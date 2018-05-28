#pragma once
#include "basic_session.hpp"
#include "message.hpp"

namespace protoserv
{
/*
@description
Asynchronous server session
*/
class server_session : public basic_session<server_session>
{
public:
    std::function<void(server_session&)> onConnected;
    std::function<void(server_session&)> onDisconnected;
    std::function<void(server_session&, const Message&)> onMessage;

    explicit server_session(tcp::socket socket, boost::asio::io_service& svc)
        : basic_session(std::move(socket))
        , service_(svc)
    {
    }

    /*
    @brief Notifies the subscribed party about the connected event.
    */
    void notify_connected()
    {
        onConnected(*this);
    }

    // @brief Notifies the subscribed party about the disconnected event.
    void notify_disconnected()
    {
        onDisconnected(*this);
    }

    // @brief Notifies the subscribed party about the incoming message event
    void notify_message(const Message& message)
    {
        onMessage(*this, message);
    }

    // @brief Attemps the re-establish the connection
    void handle_disconnected_session()
    {
        reconnect();
    }

    // @brief Connects to the given endpoint
    void connect(const tcp::endpoint& endpoint)
    {
        remote_endpoint_ = endpoint;
        reconnect();
    }

    // @brief Asynchrounously reconnectes the previously defined endpoint
    void reconnect()
    {
        using boost::system::error_code;

        auto addr = remote_endpoint_;
        async_connect(addr, [addr, this](error_code ec)
        {
            if (!ec)
            {
                // if successful, starts asynchronous IO
                start();
            }
            else
            {
                // if failed, sleeps for the designated time and then makes another attempt
                auto timer = std::make_unique<boost::asio::deadline_timer>(service_);
                timer->expires_from_now(boost::posix_time::milliseconds(500));

                auto& t = *timer;

                t.async_wait([this](error_code err)
                {
                    reconnect();
                });
            }
        });
    }

private:
    boost::asio::io_service& service_;
    tcp::endpoint remote_endpoint_;
};

} // namespace protoserv
