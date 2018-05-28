#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>
#include "module.hpp"
#include "async_client.hpp"

#include "runner.hpp"

#include "protobuf_messages/messages.pb.h"

namespace test = tests;

template <typename T>
using Runner = test::Runner<T>;

using Client = protoserv::async_client<test::SimpleClientMessage>;

namespace
{
class EchoServer : public module_base<EchoServer, test::SimpleClientMessage>
{
public:
    void onMessage(ClientConnection& conn, test::SimpleClientMessage& msg)
    {
        test::SimpleClientMessage reply;
        reply.set_timestamp(msg.timestamp());
        reply.set_payload(msg.payload());

        send_message(conn, reply);
    }
};
}

struct server_events_fixture
{
    enum
    {
        SERVER_PORT = 5999,
        TEST_TIMESTAMP = 121212
    };

    server_events_fixture()
    {
        testMessage.set_timestamp(TEST_TIMESTAMP);
    }

    test::SimpleClientMessage testMessage;
};

BOOST_FIXTURE_TEST_SUITE(server_events_test, server_events_fixture, *boost::unit_test::enabled())

BOOST_AUTO_TEST_CASE(server_acknowledges_new_client)
{
    class Server : public module_base<Server, test::SimpleClientMessage>
    {
    public:
        void onConnected(ClientConnection& conn)
        {
            test::SimpleClientMessage reply;
            reply.set_timestamp(12345);
            send_message(conn, reply);
        }
    };

    Runner<Server> server;
    server.run_in_background(4999);

    Client client;
    client.connect(4999);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(12345, message.timestamp());
}

BOOST_AUTO_TEST_CASE(server_is_notified_when_client_disconnects)
{
    class Server : public module_base<Server, test::SimpleClientMessage>
    {
    public:
        void onDisconnected(ClientConnection& conn)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                disconnected = true;
            }
            cv.notify_all();
        }

        bool waitDisconnected()
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait_for(lock, std::chrono::seconds(1));
            return disconnected;
        }

        std::mutex mutex;
        std::condition_variable cv;
        bool disconnected = false;
    };

    Runner<Server> server;
    server.run_in_background(4999);

    Client client;
    client.connect(4999);
    client.disconnect();

    BOOST_CHECK(server->waitDisconnected());
}

BOOST_AUTO_TEST_CASE(connects_to_another_server)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        Server()
        {
            connect_to_server("127.0.0.1", 5999);
        }

        void onConnected(ServerConnection& conn)
        {
            connected = true;
        }

        bool connected = false;
    };

    Runner<EchoServer> echo;
    echo.run_in_background(5999);

    Runner<Server> srv;
    srv.run_in_background(6000);

    BOOST_CHECK(srv->connected);
}

BOOST_AUTO_TEST_CASE(disconnects_server)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        Server()
        {
            connect_to_server("127.0.0.1", 5999);
        }

        void onDisconnected(ServerConnection& conn)
        {
            disconnected = true;
        }

        bool disconnected = false;
    };

    auto echo = std::make_unique<Runner<EchoServer>>();
    echo->run_in_background(5999);

    Runner<Server> srv;
    srv.run_in_background(6000);

    echo.reset();
    srv.join();

    BOOST_CHECK(srv->disconnected);
}

BOOST_AUTO_TEST_CASE(initializes_and_deinitializes_server)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        void onInitialized()
        {
            initialized = true;
        }

        void onDeinitialized()
        {
            deinitialized = true;
        }

        bool initialized = false;
        bool deinitialized = false;
    };

    Runner<Server> srv;
    srv.run_in_background(5999);
    srv.join();

    BOOST_CHECK(srv->initialized);
    BOOST_CHECK(srv->deinitialized);
}

BOOST_AUTO_TEST_CASE(loads_configuration)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        void onConfiguration(const protoserv::Options& conf)
        {
            configured = true;
        }

        bool configured = false;
    };

    Runner<Server> srv;
    srv.run_in_background(5999);
    srv.join();

    BOOST_CHECK(srv->configured);
}

BOOST_AUTO_TEST_CASE(can_pass_connection_by_pointer)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        void onMessage(ClientConnection* conn, test::SimpleClientMessage msg)
        {
            send_message(conn, msg);
        }

        void onConnected(ClientConnection*) // NOLINT(readability/casting)
        {
            connected = true;
        }

        void onDisconnected(ClientConnection*) // NOLINT(readability/casting)
        {
            disconnected = true;
        }

        bool connected = false;
        bool disconnected = false;
    };

    Runner<Server> srv;
    srv.run_in_background(5999);

    Client client;
    client.connect(5999);
    srv->send_message(client, testMessage);
    client.wait_message<test::SimpleClientMessage>();

    client.disconnect();
    srv.join();

    BOOST_CHECK(srv->connected);
    BOOST_CHECK(srv->disconnected);
}

BOOST_AUTO_TEST_CASE(handles_incoming_message_with_no_associated_connection)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        void onMessage(test::SimpleClientMessage& msg)
        {
            messageReceived = true;
        }

        bool messageReceived = false;
    };

    #pragma warning(disable:4455)
    using std::literals::chrono_literals::operator "" ms;
    #pragma warning(default:4455)

    Runner<Server> srv;
    srv.run_in_background(5999);

    Client client;
    client.connect(5999);
    srv->send_message(client, testMessage);
    std::this_thread::sleep_for(100ms);

    client.disconnect();
    srv.join();

    BOOST_CHECK(srv->messageReceived);
}

BOOST_AUTO_TEST_CASE(notifies_disconnected_client_when_server_gets_stopped)
{
    class Server : public module_base<Server, test::SimpleClientMessage>
    {
    public:
        void onDisconnected(ClientConnection& conn)
        {
            disconnected = true;
        }

        bool disconnected = false;
    };

    Runner<Server> server;
    server.run_in_background(4999);


    protoserv::async_client<test::SimpleClientMessage> client;
    client.connect(4999);
    client.send(testMessage);

    // Server may not be able to receive and process the message before join is called.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    server.join();

    BOOST_CHECK(server->disconnected);
}


BOOST_AUTO_TEST_SUITE_END()
