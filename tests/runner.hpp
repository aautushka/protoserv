#pragma once
#include <thread>
#include <string>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#include "server.hpp"

namespace tests
{
namespace fs = boost::filesystem;

template <typename Server>
class Runner
{
public:
    ~Runner()
    {
        join();
    }

    void join()
    {
        server_.set_active(false);

        if (background_worker_.joinable())
        {
            background_worker_.join();
        }
    }

    void run_in_background(protoserv::Options& conf)
    {
        auto srv = &server_;

        background_worker_ = std::thread
        {
            [conf, srv]()
            {
                srv->run_server("magic", conf);
            }
        };
    }

    void run_in_background(uint16_t port)
    {
        using protoserv::Options;
        Options conf;
        conf["Port"] = boost::lexical_cast<std::string>(port);
        server_port_ = port;
        run_in_background(conf);
    }

    void run(protoserv::Options& conf)
    {
        server_.run_server("magic", conf);
    }

    void run(uint16_t port)
    {
        protoserv::Options conf;
        conf["Port"] = boost::lexical_cast<std::string>(port);
        run(conf);
    }

    void wait_until_server_ready()
    {
        using tcp = boost::asio::ip::tcp;

        auto ip = boost::asio::ip::address::from_string("127.0.0.1");
        auto endpoint = tcp::endpoint(ip, server_port_);

        boost::asio::io_service service;
        tcp::socket socket(service);
        socket.open(tcp::v4());
        socket.set_option(tcp::no_delay(true));
        socket.set_option(boost::asio::socket_base::linger(true, 0));

        boost::system::error_code ec;
        do
        {
            socket.connect(endpoint, ec);
        }
        while (ec);
    }

    void wait()
    {
        background_worker_.join();
    }

    void configure(const fs::path& configPath)
    {
        config_path_ = configPath;
    }

    Server* operator ->()
    {
        return &server_;
    }

private:

    fs::path config_path_;
    std::thread background_worker_;
    Server server_;
    uint16_t server_port_ = 0;
};

} // namespace tests

