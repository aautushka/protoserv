#include "server.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>

#include <iomanip>
#include <string>
#include <vector>

namespace protoserv
{

static void set_real_time_process_priority()
{
    // TODO
}

app_server::app_server()
    : acceptor_(service_)
    , next_socket_(service_)
    , resolver_(service_)
    , stdin_(service_)
{
    // set empty client/server event handlers
    onClientConnected = [](auto&) {};
    onClientMessage = [](auto&, auto) {};
    onClientDisconnected = [](auto&) {};

    onServerConnected = [](auto&) {};
    onServerMessage = [](auto&, auto) {};
    onServerDisconnected = [](auto&) {};

    onCommandReceived = [](auto) {};

    onApplicationInitialized = [] {};
    onApplicationDeinitialized = [] {};

    onConfigurationLoaded = [](auto&) {};
}

app_server::~app_server()
{

}

std::string get_opt(const Options& opts, std::string key, std::string default_value = "")
{
    auto it = opts.find(key);
    if (it != opts.end())
    {
        return it->second;
    }

    return default_value;
}

void app_server::run_server(const std::string&  app_name, const Options& opts)
{
    auto ip = get_opt(opts, "Ip", "127.0.0.1");
    auto port_str = get_opt(opts, "Port", "65535");
    auto port = boost::lexical_cast<uint16_t>(port_str);


    acceptor_ = tcp::acceptor(service_, tcp::endpoint(tcp::v4(), port));
    do_accept();
    do_read_stdin();

    set_real_time_process_priority();

    onApplicationInitialized();
    onConfigurationLoaded(opts);

    service_.run();

    clients_.foreach([](auto session)
    {
        if (session->connected())
        {
            session->notify_disconnected();
        }
    });

    servers_.foreach([](auto session)
    {
        if (session->connected())
        {
            session->notify_disconnected();
        }
    });

    onApplicationDeinitialized();
}

void app_server::set_active(bool active)
{
    if (!active)
    {
        service_.dispatch([this]()
        {
            close_all_connections_and_stop();
        });
    }
}

void app_server::async_read_stream(
    std::istream& stream, std::function<void(const command&)> handler)
{
    stdin_.async_read(stream, handler);
}

void app_server::do_accept()
{
    acceptor_.async_accept(next_socket_,
                           [this](boost::system::error_code err)
    {
        if (!err)
        {
            auto nativeHandle = next_socket_.native_handle();
            auto session = clients_.create(std::move(next_socket_), *this);
            session->start();

            do_accept();
        }
    });
}

void app_server::do_read_stdin()
{
    stdin_.async_read(std::cin,
                      [this](const command & cmd)
    {
        handle_default_command(cmd);
        onCommandReceived(cmd);
        do_read_stdin();
    });
}

void app_server::close_all_connections_and_stop()
{
    acceptor_.close();
    service_.stop();
}

void app_server::handle_default_command(const command& cmd)
{
    auto& key = cmd.name();

    if (!key.compare("help"))
    {
        std::cout
                << std::left << std::setw(12) << "help" << " show this message\r\n"
                << std::left << std::setw(12) << "exit" << " terminate server\r\n"
                << std::left << std::setw(12) << "exception" << " raise an exception\r\n"
                << "\r\n";

    }
    else if (!key.compare("exit"))
    {
        set_active(false);
    }
    else if (!key.compare("die"))
    {
    }
}

app_server::ServerConnection& app_server::connect_to_server(
    const std::string& ip, uint16_t port,
    decltype(onServerMessage) handler)
{
    return connect_to_server(ip, port, handler,
                             [this](auto & session)
    {
        notify_connected(session);
    },
    [this](auto & session)
    {
        notify_disconnected(session);
    });
}

app_server::ServerConnection& app_server::connect_to_server(
    const std::string& ip, uint16_t port,
    decltype(onServerMessage) messageHandler,
    decltype(onServerConnected) connectHandler,
    decltype(onServerDisconnected) disconnectHandler
)
{
    auto ipaddr = boost::asio::ip::address::from_string(ip);
    auto endpoint = tcp::endpoint(ipaddr, port);

    tcp::socket socket(service_);
    socket.connect(endpoint);
    auto session = servers_.create(std::move(socket), service_);
    session->onMessage = messageHandler;
    session->onConnected = connectHandler;
    session->onDisconnected = disconnectHandler;
    session->start();

    return *session;
}

app_server::ServerConnection& app_server::connect_to_server(
    const std::string& ip, uint16_t port)
{
    return connect_to_server(ip, port, onServerMessage, onServerConnected, onServerDisconnected);
}

app_server::ServerConnection& app_server::async_connect(
    const std::string& ip, uint16_t port,
    decltype(onServerMessage) handler)
{
    return async_connect(ip, port, handler, [](auto&) {}, [](auto&) {});
}

app_server::ServerConnection& app_server::async_connect(
    const std::string& ip, uint16_t port,
    decltype(onServerMessage) messageHandler,
    decltype(onServerConnected) connectHandler,
    decltype(onServerDisconnected) disconnectHandler
)
{
    tcp::socket socket(service_);
    auto session = servers_.create(std::move(socket), service_);
    session->onMessage = messageHandler;
    session->onConnected = connectHandler;
    session->onDisconnected = disconnectHandler;

    auto ipaddr = boost::asio::ip::address::from_string(ip);
    auto endpoint = tcp::endpoint(ipaddr, port);
    session->connect(endpoint);

    return *session;
}

app_server::ServerConnection& app_server::async_connect(const std::string& ip, uint16_t port)
{
    return async_connect(ip, port, onServerMessage, onServerConnected, onServerDisconnected);
}
} // namespace protoserv
