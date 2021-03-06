#pragma once

#include "client_session.hpp"
#include "message.hpp"
#include "messagebuf.hpp"
#include "meta_protocol.hpp"
#include "dispatch_table.hpp"
#include <string>
#include <algorithm>
#include <list>

namespace protoserv
{
template <
    template<typename, typename ...> typename protocol_operations,
    class Protocol, class ...Messages
    >
class async_client_pack;

// @brief Asychronous protobuf-aware TCP/IP client
template <
    template<typename, typename ... > typename protocol_operations,
    class Protocol, class ... Messages
    >
class async_client_pack<protocol_operations, meta::subprotocol<Protocol, Messages...>>
{
public:
    using tcp = boost::asio::ip::tcp;
    using protocol_pack = meta::subprotocol<Protocol, Messages...>;
    using self_type = async_client_pack<protocol_operations, protocol_pack>;
    using session_type = basic_client_session<self_type>;
    using proto_ops = protocol_operations<protocol_pack, Messages...>;

    // @brief Creates object
    async_client_pack()
        : resolver_(service_)
    {
    }

    // @brief Cancels all pending async operations
    ~async_client_pack()
    {
        dispatcher_.cancel();
    }

    // @brief Synchronously connects to the remote endpoint
    void connect(uint16_t port)
    {
        connect("127.0.0.1", port);
    }

    // @brief Connect to the remote endpoint
    void connect(const std::string& ipaddr, uint16_t port)
    {
        connect(ipaddr, port, 1);
    }

    void connect(const std::string& ipaddr, uint16_t port, int attempts)
    {
        assert(!session_);

        if (attempts < 0)
        {
            return;
        }

        auto ip = boost::asio::ip::address::from_string(ipaddr);
        auto endpoint = tcp::endpoint(ip, port);
        /* auto it = resolver_.resolve(endpoint); */

        session_ = std::make_unique<session_type>(tcp::socket(service_), *this);

        tcp::socket socket(service_);
        socket.open(tcp::v4());
        socket.set_option(tcp::no_delay(true));
        socket.set_option(boost::asio::socket_base::linger(true, 0));

        boost::system::error_code ec;
        for (int i = 0; i < attempts - 1; ++i)
        {
            socket.connect(endpoint, ec);
            if (!ec)
            {
                session_->connect(std::move(socket));
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        socket.connect(endpoint);
        session_->connect(std::move(socket));
    }

    void wait_connect(const std::string& addr, uint16_t port)
    {
        connect(addr, port, 0x7fffffff);
    }

    void wait_connect(uint16_t port)
    {
        connect("127.0.0.1", port, 0x7fffffff);
    }


    // @brief Disconnects client and closes all pending requests
    void disconnect()
    {
        if (session_)
        {
            dispatcher_.cancel();
            session_->kill();

            // flush any outstanding requests
            service_.reset();
            service_.run();
            session_.reset();
        }
    }

    template <typename T>
    auto get_message_id()
    {
        using message_id_type = typename proto_ops::template get_message_id<T>;
        return message_id_type::value;
    }

    // @brief Sends protobuf message to the remote e
    template <typename T>
    void send(const T& message)
    {
        session_->send(get_message_id<T>(), message);
    }

    template <typename T>
    void send(int messageId, const T& message)
    {
        //FIXME: this is necessary for tests now
        session_->send(messageId, message);
    }

    // @brief Waits for message of given type T
    template <typename T>
    void wait_message(T& t)
    {
        while (true)
        {
            for (auto i = queue_.begin(); i != queue_.end(); ++i)
            {
                auto& m = **i;
                if (get_message_id<T>() == m.type)
                {
                    t.ParseFromArray(m.data, m.size);
                    queue_.erase(i);
                    return;
                }
            }

            run_one();
        }
    }

    // @brief Checks in there is a message of given type T on the queue
    template <typename T>
    bool try_receive(T& t)
    {
        for (auto i = queue_.begin(); i != queue_.end(); ++i)
        {
            auto& m = **i;
            if (get_message_id<T>() == m.type)
            {
                t.ParseFromArray(m.data, m.size);
                queue_.erase(i);
                return true;
            }
        }

        run_one();
        return false;
    }

    // @brief Waits for incoming message of type T
    template <typename T>
    T wait_message()
    {
        T t;
        wait_message(t);
        return std::move(t);
    }

    // @brief Asynchronously receive a message and call the handler
    template <typename Handler>
    void receive(Handler handler)
    {
        dispatcher_.subscribe(std::move(handler));
    }

    // @brief Run the client
    void run()
    {
        do
        {
            run_one();
        }
        while (!dispatcher_.done());
    }

    // @brief Read some data from the socket and return
    void run_one()
    {
        session_->read_some();
        service_.reset();
        service_.run();
    }

    // @brief Connection event handler. TODO: remove from public
    void notify_connected(session_type&)
    {
    }

    // @brief Disconnected event handler. TODO: remove from public
    void notify_disconnected(session_type&)
    {
        disconnect();
    }

    // @brief Message event handler. TODO: remove from public
    void notify_message(session_type&, const Message& msg)
    {
        if (!dispatch_message(msg))
        {
            queue_.push_back(messagebuf::copy(msg));
        }
    }

    // TODO: remove from public
    void remove_session(session_type* session)
    {
        disconnect();
    }

    // TODO: remove from public
    void is_valid_session(session_type* session)
    {
        return session == session_.get();
    }

private:
    // @brief Despatches received message to appropriate handler
    bool dispatch_message(const Message& msg)
    {
        return proto_ops::dispatcher::dispatch(dispatcher_, msg);
    }

    boost::asio::io_service service_;
    tcp::resolver resolver_;

    std::unique_ptr<session_type> session_;
    std::list<messagebuf> queue_;
    typename proto_ops::table dispatcher_;
};

} // namespace protoserv
