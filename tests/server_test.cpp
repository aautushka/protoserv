#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

#include "module.hpp"

#include "runner.hpp"
#include "async_client.hpp"

#include "protobuf_messages/messages.pb.h"

namespace test = tests;
namespace app = protoserv;

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

struct server_fixture 
{
    enum
    {
        SERVER_PORT = 5999,
        TEST_TIMESTAMP = 121212
    };

    server_fixture()
    {
        testMessage.set_timestamp(TEST_TIMESTAMP);
    }

    test::SimpleClientMessage testMessage;
};

BOOST_FIXTURE_TEST_SUITE(server_test, server_fixture, *boost::unit_test::enabled())

/* BOOST_AUTO_TEST_CASE(reads_stdin) */
/* { */
/*     struct Server : public module_base<Server, test::SimpleClientMessage> */
/*     { */
/*         Server() */
/*         { */
/*         } */

/*         void onCommand(const command& cmd) */
/*         { */
/*             command = cmd.raw(); */
/*         } */

/*         std::string command; */
/*     }; */

/*     class replace */
/*     { */
/*     public: */
/*         replace(std::istream& stream, std::string data) */
/*             : recepient_(stream) */
/*             , donor_(data) */
/*             , backup_(stream.rdbuf()) */
/*         { */
/*             recepient_.rdbuf(donor_.rdbuf()); */
/*         } */

/*         ~replace() */
/*         { */
/*             recepient_.rdbuf(backup_); */
/*         } */

/*     private: */
/*         std::istream& recepient_; */
/*         std::istringstream donor_; */
/*         std::streambuf* backup_; */
/*     }; */

/*     replace cin_replace(std::cin, "command\n"); */

/*     Runner<Server> server; */
/*     server.run_in_background(4999); */

/*     // this would delay io_service so we have a chance of receiving onCommand */
/*     Client client; */
/*     client.connect(4999); */
/*     client.disconnect(); */
/*     server.join(); */

/*     BOOST_CHECK_EQUAL(std::string("command"), server->command); */
/* } */

BOOST_AUTO_TEST_CASE(asynchronously_connects_to_server)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        Server()
        {
            async_connect("127.0.0.1", 5999);
        }

        void onConnected(ServerConnection& conn)
        {
            connected = true;
        }

        std::atomic<bool> connected = false;
    };

    Runner<EchoServer> echo;
    echo.run_in_background(5999);

    Runner<Server> srv;
    srv.run_in_background(6000);

    // make sure echo server is up
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    BOOST_CHECK(srv->connected);
}

BOOST_AUTO_TEST_CASE(reconnects_to_server_after_connection_broken)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        Server()
        {
            async_connect("127.0.0.1", 5999);
        }

        void onConnected(ServerConnection& conn)
        {
            ++connected;
        }

        void onDisconnected(ServerConnection& conn)
        {
            ++disconnected;
        }

        int connected = 0;
        int disconnected = 0;
    };

    Runner<Server> srv;
    srv.run_in_background(6000);

    for (int i = 0; i < 3; ++i)
    {
        auto echo = std::make_unique<Runner<EchoServer>>();
        echo->run_in_background(5999);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        echo.reset();
    }

    BOOST_CHECK_EQUAL(3, srv->connected);
    BOOST_CHECK_EQUAL(3, srv->disconnected);
}

BOOST_AUTO_TEST_CASE(disconnects_inactive_client)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        void onDisconnected(ClientConnection& conn)
        {
            disconnected = true;
        }

        void onMessage(ClientConnection& conn, test::SimpleClientMessage msg)
        {
            send_message(conn, msg);
        }

        std::atomic<bool> disconnected = false;
    };

    Runner<Server> srv;
    srv.run_in_background(5999);

    Client client;
    client.connect(5999);
    srv->send_message(client, testMessage);
    client.wait_message<test::SimpleClientMessage>();

    srv->async_disconnect_inactive_clients(std::chrono::microseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    BOOST_CHECK(srv->disconnected.load());
}

BOOST_AUTO_TEST_CASE(disconnects_inactive_server)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
    };

    struct Proxy : public module_base<Proxy, test::SimpleClientMessage>
    {
        Proxy()
        {
            connect_to_server("127.0.0.1", 5999);
        }

        void onDisconnected(ServerConnection& conn)
        {
            disconnected = true;
        }

        std::atomic<bool> disconnected = false;
    };

    Runner<Server> srv;
    srv.run_in_background(5999);

    Runner<Proxy> prox;
    prox.run_in_background(6000);

    prox->async_disconnect_inactive_servers(std::chrono::microseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    BOOST_CHECK(prox->disconnected.load());
}

BOOST_AUTO_TEST_CASE(holds_reference_to_a_session_preventing_it_from_destruction)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        void onMessage(ClientConnection& conn, test::SimpleClientMessage& msg)
        {
            ref = conn.take_ownership();
            send_message(conn, msg);
        }

        void onDisconnected(ClientConnection& conn)
        {
            disconnected = true;
        }

        std::atomic<bool> disconnected = true;
        ClientConnection::reference ref;
    };

    Runner<Server> srv;
    srv.run_in_background(5999);

    Client client;
    client.connect(5999);
    srv->send_message(client, testMessage);
    client.wait_message<test::SimpleClientMessage>();
    client.disconnect();
    srv.join();

    BOOST_CHECK(srv->disconnected);
}

BOOST_AUTO_TEST_CASE(attempts_to_send_a_message_to_a_dead_session)
{
    #pragma warning(disable:4455)
    using std::literals::chrono_literals::operator "" ms;
    #pragma warning(default:4455)

    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        void onMessage(ClientConnection& conn, test::SimpleClientMessage& msg)
        {
            ref = conn.take_ownership();
            send_message(conn, msg);

            async_wait(100ms, [this, msg]()
            {
                send_message(*ref, msg);
                timer_fired = true;
            });
        }

        void onDisconnected(ClientConnection&)
        {
            disconnected++;
        }

        ClientConnection::reference ref;
        bool timer_fired = false;
        int disconnected = 0;
    };

    Runner<Server> srv;
    srv.run_in_background(5999);

    Client client;
    client.connect(5999);
    srv->send_message(client, testMessage);
    client.wait_message<test::SimpleClientMessage>();
    client.disconnect();

    std::this_thread::sleep_for(300ms);
    srv.join();

    BOOST_CHECK(srv->timer_fired);
    BOOST_CHECK_EQUAL(1, srv->disconnected);
}

BOOST_AUTO_TEST_CASE(no_server_events_despite_attemps_to_send_message_to_it)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        void onMessage(ClientConnection& conn, test::SimpleClientMessage& msg)
        {
            auto& server = async_connect("127.0.0.1", 6000);
            send_message(server, msg);
            send_message(conn, msg);
        }

        void onConnected(ServerConnection& conn)
        {
            connected = true;
        }

        void onDisconnected(ServerConnection& conn)
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

    BOOST_CHECK(!srv->connected);
    BOOST_CHECK(!srv->disconnected);
}

BOOST_AUTO_TEST_CASE(passes_user_data_across_events)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        auto onMessage(ClientConnection& conn, test::SimpleClientMessage& msg)
        {
            userData = conn.get_user_data<int>();
            return msg;
        }

        void onConnected(ClientConnection& conn)
        {
            conn.set_user_data(0);
            conn.get_user_data<int>() = 123;
        }

        int userData = 0;
    };

    Runner<Server> srv;
    srv.run_in_background(5999);

    Client client;
    client.connect(5999);
    srv->send_message(client, testMessage);
    client.wait_message<test::SimpleClientMessage>();
    client.disconnect();
    srv.join();

    BOOST_CHECK_EQUAL(123, srv->userData);
}


BOOST_AUTO_TEST_SUITE_END()
