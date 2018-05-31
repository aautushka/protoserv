#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

#include "async_client.hpp"
#include "protobuf_messages/messages.pb.h"
#include "runner.hpp"
#include "echo_server.hpp"

using std::literals::chrono_literals::operator "" ms;

namespace
{
using EchoProtocol = meta::protocol <
                     tests::SimpleClientMessage
                     >;

struct Echo : public module_base<Echo, EchoProtocol>
{
    template <typename Message>
    void onMessage(ClientConnection& conn, Message& msg)
    {
        send_message(conn, msg);
    }
};
}

template <typename T>
using Runner = tests::Runner<T>;

struct async_handler_fixture
{
    protoserv::async_client<EchoProtocol> client;

    tests::SimpleClientMessage make_message(int timestamp)
    {
        tests::SimpleClientMessage ret;
        ret.set_timestamp(timestamp);
        return ret;
    }

    template<typename Message, typename T>
    Message make_message(T t)
    {
        Message m;
        m.set_data(t);
        return m;
    }
};

BOOST_FIXTURE_TEST_SUITE(async_handler_test, async_handler_fixture)

BOOST_AUTO_TEST_CASE(passes_message_via_async_handler)
{
    struct Proxy : module_base<Proxy, EchoProtocol>
    {
        Proxy()
        {
            connect_to_server("127.0.0.1", 4999);
        }

        void onMessage(ClientConnection& conn, tests::SimpleClientMessage& msg)
        {
            auto handler = handle_server_async(*connection);
            handler->send(msg);
            handler->receive(
                [&conn, handler](tests::SimpleClientMessage & msg, auto err)
            {
                if (!err)
                {
                    send_message(conn, msg);
                }
            });
        }

        void onConnected(ServerConnection* conn)
        {
            connection = conn;
        }

        ServerConnection* connection = nullptr;
    };

    Runner<Echo> echo;
    echo.run_in_background(4999);
    echo.wait_until_server_ready();

    Runner<Proxy> proxy;
    proxy.run_in_background(5000);

    std::this_thread::sleep_for(200ms);

    client.wait_connect(5000);
    client.send(make_message(12345));

    auto msg = client.wait_message<tests::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(12345, msg.timestamp());
}

BOOST_AUTO_TEST_CASE(cancels_outstanding_requests_on_connection_failure)
{
    struct Proxy : module_base<Proxy, EchoProtocol>
    {
        Proxy()
        {
            connect_to_server("127.0.0.1", 4999);
        }

        void onMessage(ClientConnection& conn, tests::SimpleClientMessage& msg)
        {
            auto handler = handle_server_async(*connection);
            connection->close();

            handler->receive(
                [this, handler](tests::SimpleClientMessage & msg, auto err)
            {
                if (err)
                {
                    errorOccurred = true;
                }
            });
        }

        void onConnected(ServerConnection* conn)
        {
            connection = conn;
        }

        ServerConnection* connection = nullptr;
        bool errorOccurred = false;
    };

    struct Echo : module_base<Echo, EchoProtocol>
    {
        void onMessage(ClientConnection& conn, tests::SimpleClientMessage& msg)
        {
            conn.close();
        }
    };

    Runner<Echo> echo;
    echo.run_in_background(4999);
    echo.wait_until_server_ready();

    Runner<Proxy> proxy;
    proxy.run_in_background(5000);

    client.wait_connect(5000);
    client.send(make_message(12345));

    using std::literals::chrono_literals::operator "" ms;
    std::this_thread::sleep_for(300ms);
    proxy.join();

    BOOST_CHECK(proxy->errorOccurred);
}

BOOST_AUTO_TEST_CASE(cancels_request_immediately_if_no_connection_established)
{
    struct Proxy : module_base<Proxy, EchoProtocol>
    {
        void onMessage(ClientConnection& conn, tests::SimpleClientMessage& msg)
        {
            auto handler = handle_server_async("127.0.0.1", 4999);
            handler->receive(
                [&conn, handler, this](tests::SimpleClientMessage & msg, auto err)
            {
                if (err)
                {
                    errorOccurred = true;
                }
            });
        }

        bool errorOccurred = false;
    };

    Runner<Proxy> proxy;
    proxy.run_in_background(5000);

    client.wait_connect(5000);
    client.send(make_message(12345));
    using std::literals::chrono_literals::operator "" ms;
    std::this_thread::sleep_for(300ms);
    proxy.join();

    BOOST_CHECK(proxy->errorOccurred);
}

BOOST_AUTO_TEST_SUITE_END()
