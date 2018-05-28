#pragma once

#include "server_session.hpp"
#include "client_session.hpp"
#include "object_pool.hpp"
#include "timer.hpp"
#include "async_stdin.hpp"
#include <string>
#include <vector>

namespace protoserv
{

using Options = std::map<std::string, std::string>;
using Option = Options::value_type;

class server_error : public std::exception {};

// @brief Single-threaded, asynchronous TCP/IP server
class app_server
{
public:
    using tcp = boost::asio::ip::tcp;
    using error_code = boost::system::error_code;

    using client_session = basic_client_session<app_server>;
    using ClientConnection = client_session;
    using ServerConnection = server_session;

    app_server();
    ~app_server();

    // @brief Runs server in the current thread
    void run_server(const std::string& app_name, const Options& opts);

    // @brief When passed false, terminates the server
    void set_active(bool active);

    // @brief Client events
    std::function<void(client_session&, const Message&)> onClientMessage;
    std::function<void(client_session&)> onClientConnected;
    std::function<void(client_session&)> onClientDisconnected;

    // @brief Server events
    std::function<void(server_session&, const Message&)> onServerMessage;
    std::function<void(server_session&)> onServerConnected;
    std::function<void(server_session&)> onServerDisconnected;

    // @brief Stdin command event
    std::function<void(const command&)> onCommandReceived;

    // @brief Application events
    std::function<void(void)> onApplicationInitialized;
    std::function<void(void)> onApplicationDeinitialized;
    std::function<void(const Options&)> onConfigurationLoaded;

    // @brief Notifies about new protobuf message incoming from the client
    void notify_message(client_session& session, const Message& msg)
    {
        onClientMessage(session, msg);
    }

    // @brief Notifies about client connection being established
    void notify_connected(client_session& session)
    {
        onClientConnected(session);
    }

    // @brief Notifies about client connection being closed
    void notify_disconnected(client_session& session)
    {
        onClientDisconnected(session);
    }

    // @brief Notifies about new server message
    void notify_message(server_session& session, const Message& msg)
    {
        onServerMessage(session, msg);
    }

    // @brief Notifies about new server connection
    void notify_connected(server_session& session)
    {
        onServerConnected(session);
    }

    // @brief Notifies about server connection being closed
    void notify_disconnected(server_session& session)
    {
        onServerDisconnected(session);
    }

    // @brief Subscribes to client messages
    void subscribe_client(decltype(onClientMessage) handler)
    {
        onClientMessage = handler;
    }

    // @brief Subscribes to server messages
    void subscribe_server(decltype(onServerMessage) handler)
    {
        onServerMessage = handler;
    }

    // @brief Synchronously connects to server
    ServerConnection& connect_to_server(
        const std::string& ip, uint16_t port, decltype(onServerMessage) handler);

    // @brief Synchronously connect to server
    ServerConnection& connect_to_server(
        const std::string& ip, uint16_t port,
        decltype(onServerMessage) messageHandler,
        decltype(onServerConnected) connectHandler,
        decltype(onServerDisconnected) disconnectHandler
    );

    // @brief Synchronously connects to server
    ServerConnection& connect_to_server(const std::string& ip, uint16_t port);

    // @brief Asynchronously connects to server
    ServerConnection& async_connect(
        const std::string& ip, uint16_t port, decltype(onServerMessage) handler);

    // @brief Asynchronously connects to server
    ServerConnection& async_connect(const std::string& ip, uint16_t port);

    // @brief Asynchronously connects to server
    ServerConnection& async_connect(
        const std::string& ip, uint16_t port,
        decltype(onServerMessage) messageHandler,
        decltype(onServerConnected) connectHandler,
        decltype(onServerDisconnected) disconnectHandler
    );

    // @brief Asynchronously waits for timer event and fires the give handler
    template <typename Timeout>
    void async_wait(Timeout timeout, std::function<void()> handler)
    {
        auto timer = std::make_shared<boost::asio::deadline_timer>(service_);
        auto microsec = std::chrono::duration_cast<std::chrono::microseconds>(timeout);
        timer->expires_from_now(boost::posix_time::microseconds(microsec.count()));

        using boost::system::error_code;
        auto& t = *timer;

        t.async_wait([timer{ std::move(timer) }, handler{ std::move(handler) }](error_code err)
        {
            if (!err)
            {
                handler();
            }
        });
    }

    // @brief Creates periodic asynchronous timer event
    template <typename Period>
    void async_wait_period(Period period, std::function<void()> handler)
    {
        auto timer = std::make_shared<boost::asio::deadline_timer>(service_);

        auto microsec = std::chrono::duration_cast<std::chrono::microseconds>(period);

        auto pp = boost::posix_time::microseconds(microsec.count());
        do_async_wait_period(std::move(timer), pp, handler);
    }

    // @brief Closes inactive client connections
    template <typename Duration>
    void async_disconnect_inactive_clients(Duration duration)
    {
        service_.dispatch([this, duration]()
        {
            disconnect_inactive_clients(duration);
        });
    }

    // @brief Closes inactive server connections
    template <typename Duration>
    void async_disconnect_inactive_servers(Duration duration)
    {
        service_.dispatch([this, duration]()
        {
            disconnect_inactive_servers(duration);
        });
    }

    // @brief Creates asynchronous timer
    template <typename Period>
    std::shared_ptr<Timer> create_timer(Period period, std::function<void(void)> handler)
    {
        auto timer = std::make_shared<Timer>(service_);
        timer->start(period, handler);
        return std::move(timer);
    }

    // @brief Destroys the client session
    void remove_session(client_session* session)
    {
        clients_.destroy(session);
    }

    // @brief Destroys the server session
    void remove_session(server_session* session)
    {
        servers_.destroy(session);
    }

    // @brief Check if valid client session
    bool is_valid_session(const client_session* session) const
    {
        return clients_.allocated(session);
    }

    // @brief Visits every client session
    template <typename Func>
    void foreach_connection(Func&& f)
    {
        clients_.foreach(std::forward<Func>(f));
    }

    // @brief Visits all user objects associated with client connections
    template <typename UserData, typename Func>
    void foreach_user_data_connection(Func&& f)
    {
        // TODO
        /* foreach_connection([](auto conn) */
        /* { */
        /*     auto user = conn->get_user_data_if<UserData>(); */
        /*     if (user) */
        /*     { */
        /*         f(*user); */
        /*     } */
        /* }); */
    }

private:
    // @brief Disconnects any stale client connections
    template <typename Duration>
    void disconnect_inactive_clients(Duration duration)
    {
        clients_.foreach([duration, this](auto session)
        {
            session->disconnect_inactive(duration);
        });
    }

    // @brief Disconnects any stale server connections
    template <typename Duration>
    void disconnect_inactive_servers(Duration duration)
    {
        servers_.foreach([duration, this](auto session)
        {
            session->disconnect_inactive(duration);
        });
    }

    // @brief Schedules a recurring timer event
    template <typename Timer, typename Period>
    static void do_async_wait_period(
        Timer&& timer, Period period, std::function<void()> handler)
    {
        timer->expires_from_now(period);

        using boost::system::error_code;
        auto& t = *timer;
        t.async_wait([t{ std::move(timer) }, h{ std::move(handler) }, period](error_code e) mutable
        {
            if (!e)
            {
                h();
                do_async_wait_period(std::move(t), period, h);
            }
        });
    }

    // @brief Asynchrounously reads from the stream and calls the handler once a well formatted command is read
    void async_read_stream(std::istream& stream, std::function<void(const command&)> handler);

    // @brief Accepts incoming connection
    void do_accept();

    // @brief Reads stdin
    void do_read_stdin();

    // @brief Closes all connection and stops the server
    void close_all_connections_and_stop();

    // @brief Handles some well-known stdin commands common to all appliations
    void handle_default_command(const command& cmd);

    boost::asio::io_service service_;
    tcp::acceptor acceptor_;
    tcp::socket next_socket_;
    tcp::resolver resolver_;
    async_stdin stdin_;

    object_pool<client_session> clients_;
    object_pool<server_session> servers_;
};

} // namespace protoserv
