#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

#include "module.hpp"
#include "runner.hpp"
#include "async_client.hpp"

#include "protobuf_messages/messages.pb.h"

namespace test = tests;

template <typename T>
using Runner = test::Runner<T>;
using Client = protoserv::async_client<test::SimpleClientMessage>;
using Timer = protoserv::Timer;

struct module_time_fixture 
{
};

BOOST_FIXTURE_TEST_SUITE(module_timer_test, module_time_fixture, *boost::unit_test::enabled())

BOOST_AUTO_TEST_CASE(schedules_delayed_event)
{
    class Server : public module_base<Server, test::SimpleClientMessage>
    {
    public:
        void onConnected(ClientConnection& conn)
        {
            async_wait(std::chrono::milliseconds(1), [&conn]()
            {
                test::SimpleClientMessage reply;
                reply.set_timestamp(12345);
                send_message(conn, reply);
            });
        }
    };

    Runner<Server> server;
    server.run_in_background(4999);

    Client client;
    client.wait_connect(4999);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(12345, message.timestamp());
}

BOOST_AUTO_TEST_CASE(schedules_recurring_event)
{
    class Server : public module_base<Server, test::SimpleClientMessage>
    {
    public:
        void onConnected(ClientConnection& conn)
        {
            connection = &conn;
            async_wait_period(std::chrono::milliseconds(1), [this]()
            {
                if (connection)
                {
                    test::SimpleClientMessage reply;
                    reply.set_timestamp(12345);
                    send_message(*connection, reply);
                }
            });
        }

        void onDisconnected(ClientConnection& conn)
        {
            connection = nullptr;
        }

        ClientConnection* connection;
    };

    Runner<Server> server;
    server.run_in_background(4999);

    Client client;
    client.wait_connect(4999);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(12345, message.timestamp());

    message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(12345, message.timestamp());
}

BOOST_AUTO_TEST_CASE(creates_timer)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        Server()
        {
        }

        void onConnected(ClientConnection& conn)
        {
            timer = create_timer(std::chrono::milliseconds(1), [&conn]()
            {
                test::SimpleClientMessage reply;
                reply.set_timestamp(12345);
                send_message(conn, reply);
            });
        }

        void onDisconnected(ClientConnection& conn)
        {
            timer->stop();
        }

        std::shared_ptr<Timer> timer;
    };

    Runner<Server> server;
    server.run_in_background(4999);

    Client client;
    client.wait_connect(4999);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(12345, message.timestamp());
}

BOOST_AUTO_TEST_CASE(generates_multiple_timer_events)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        Server()
        {
        }

        void onConnected(ClientConnection* conn)
        {
            connection = conn;
            create_timer(std::chrono::microseconds(1), [this]()
            {
                ++timer_called;

                if (timer_called == 10)
                {
                    test::SimpleClientMessage msg;
                    msg.set_timestamp(6789);
                    send_message(connection, msg);
                }
            });
        }

        void onDisconnected(ClientConnection* conn)
        {
            connection = nullptr;
        }

        std::atomic<int> timer_called = 0;
        ClientConnection* connection = nullptr;
    };

    Runner<Server> server;
    server.run_in_background(4999);

    Client client;
    client.wait_connect(4999);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(6789, message.timestamp());
}

BOOST_AUTO_TEST_CASE(stops_timer)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        Server()
        {
        }

        void onConnected(ClientConnection& conn)
        {
            timer = create_timer(std::chrono::microseconds(1), [this]()
            {
                ++timer_called;
                timer->pause();
            });
        }

        void onDisconnected(ClientConnection& conn)
        {
        }

        std::shared_ptr<Timer> timer;
        std::atomic<int> timer_called = 0;
    };

    Runner<Server> server;
    server.run_in_background(4999);

    Client client;
    client.wait_connect(4999);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    client.disconnect();
    server.join();

    BOOST_CHECK_EQUAL(server->timer_called.load(), 1);
}

BOOST_AUTO_TEST_CASE(resumes_timer)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        Server()
        {
        }

        void onConnected(ClientConnection& conn)
        {
            timer = create_timer(std::chrono::microseconds(1), [this]()
            {
                ++timer_called;
                timer->pause();
            });
        }

        void onDisconnected(ClientConnection& conn)
        {
            timer->resume();
        }

        std::shared_ptr<Timer> timer;
        std::atomic<int> timer_called = 0;
    };

    Runner<Server> server;
    server.run_in_background(4999);

    Client client;
    client.wait_connect(4999);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.join();

    BOOST_CHECK_GT(server->timer_called.load(), 1);
}


BOOST_AUTO_TEST_SUITE_END()
