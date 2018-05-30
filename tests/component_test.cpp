#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

#include "module.hpp"
#include "async_client.hpp"
#include "runner.hpp"

#include "protobuf_messages/messages.pb.h"

namespace test = tests;

template <typename T>
using Runner = test::Runner<T>;

using Client = protoserv::async_client<test::SimpleClientMessage, test::Type6Message>;
using TestProtocol = meta::complete_protocol<test::SimpleClientMessage>;

struct component_fixture 
{
    enum
    {
        SERVER_PORT = 5999,
        TEST_TIMESTAMP = 121212
    };

    component_fixture()
    {
        testMessage.set_timestamp(TEST_TIMESTAMP);
    }

    test::SimpleClientMessage testMessage;
};

namespace
{

template <typename Module>
struct Component1
{
    using client_connection_type = protoserv::app_server::ClientConnection;
    using server_connection_type = protoserv::app_server::ServerConnection;

    void onMessage(client_connection_type& conn, test::SimpleClientMessage& msg)
    {
        test::SimpleClientMessage reply;
        reply.set_timestamp(4321);

        Module::send_message(conn, reply);
    }
};

template <typename Module>
struct Component2
{
    using client_connection_type = protoserv::app_server::ClientConnection;
    using server_connection_type = protoserv::app_server::ServerConnection;

    void onMessage(client_connection_type& conn, test::Type6Message& msg)
    {
        test::Type6Message reply;
        reply.set_data("pong");

        Module::send_message(conn, reply);
    }
};

template <typename Module>
struct Component3
{
    using client_connection_type = protoserv::app_server::ClientConnection;
    using server_connection_type = protoserv::app_server::ServerConnection;

    void onConnected(client_connection_type& conn)
    {
        clientConnected = true;
    }

    void onDisconnected(client_connection_type& conn)
    {
        clientDisconnected = true;
    }

    void onConnected(server_connection_type& conn)
    {
        serverConnected = true;
    }

    void onDisconnected(server_connection_type& conn)
    {
        serverDisconnected = true;
    }

    void onInitialized()
    {
        initialized = true;
    }

    void onDeinitialized()
    {
        deinitialized = true;
    }

    void onConfiguration(const protoserv::Options& conf)
    {
        configured = true;
    }

    std::atomic<bool> clientConnected = false;
    std::atomic<bool> clientDisconnected = false;
    std::atomic<bool> serverConnected = false;
    std::atomic<bool> serverDisconnected = false;
    std::atomic<bool> initialized = false;
    std::atomic<bool> deinitialized = false;
    std::atomic<bool> configured = false;
};

;

} // namespace anonymous


BOOST_FIXTURE_TEST_SUITE(component_test, component_fixture, *boost::unit_test::enabled())

BOOST_AUTO_TEST_CASE(calls_component_the_right_way)
{
    struct MyServer :
        public module_component <
        MyServer,
        meta::complete_protocol<test::SimpleClientMessage>,
        Component1
        > { };


    Runner<MyServer> server;
    server.run_in_background(4999);

    Client client;
    client.connect(4999);

    server->send_message(client, testMessage);
    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(4321, message.timestamp());
}

BOOST_AUTO_TEST_CASE(talks_to_independent_components)
{
    struct MyServer :
        public module_component <
        MyServer,
        meta::complete_protocol<test::SimpleClientMessage, test::Type6Message>,
        Component1,
        Component2
        > { };

    Runner<MyServer> server;
    server.run_in_background(4999);

    Client client;
    client.connect(4999);

    server->send_message(client, testMessage);
    auto reply1 = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(4321, reply1.timestamp());

    test::Type6Message type6;
    type6.set_data("ping");
    server->send_message(client, type6);
    auto reply6 = client.wait_message<test::Type6Message>();
    BOOST_CHECK_EQUAL("pong", reply6.data());
}

BOOST_AUTO_TEST_CASE(component_is_called_when_client_gets_connected_and_disconnected)
{
    struct MyServer :
        public module_component <
        MyServer,
        meta::complete_protocol<test::SimpleClientMessage>,
        Component3
        > { };


    Runner<MyServer> server;
    server.run_in_background(4999);

    Client client;
    client.connect(4999);
    client.disconnect();
    // Server may not be able to receive and process the message before join is called.
    std::this_thread::sleep_for(std::chrono::seconds(1));
    server.join();

    BOOST_CHECK(server->query_component<Component3>().clientConnected.load());
    BOOST_CHECK(server->query_component<Component3>().clientDisconnected.load());
}

BOOST_AUTO_TEST_CASE(initializes_deinitializes_component)
{
    struct MyServer :
        public module_component <
        MyServer,
        meta::complete_protocol<test::SimpleClientMessage>,
        Component3
        > { };


    Runner<MyServer> server;
    server.run_in_background(4999);
    server.join();

    BOOST_CHECK(server->query_component<Component3>().initialized.load());
    BOOST_CHECK(server->query_component<Component3>().deinitialized.load());
}

template <class Module>
struct Component4
{
    template <class Connection>
    void make_reply(Connection& conn)
    {
        test::SimpleClientMessage msg;
        msg.set_timestamp(reply_timestamp);
        Module::send_message(conn, msg);
    }

    int reply_timestamp = 778899;
};

template <class Module>
struct Component5
{
    template <class Connection>
    void onMessage(Connection& conn, test::SimpleClientMessage&)
    {
        Module::template call_component<Component4>(*this).make_reply(conn);
    }

    template <class Connection>
    void make_reply(Connection& conn)
    {
        test::SimpleClientMessage msg;
        msg.set_timestamp(reply_timestamp);
        Module::send_message(conn, msg);
    }

    int reply_timestamp = 112233;
};


BOOST_AUTO_TEST_CASE(components_talk_to_one_another)
{
    struct MyServer :
        public module_component <
        MyServer,
        meta::complete_protocol<test::SimpleClientMessage>,
        Component4,
        Component5
        > { };


    Runner<MyServer> server;
    server.run_in_background(4999);

    Client client;
    client.connect(4999);
    server->send_message(client, testMessage);
    auto message = client.wait_message<test::SimpleClientMessage>();

    BOOST_CHECK_EQUAL(778899, message.timestamp());
}

template <class Module>
struct Component6
{
    template <class Connection>
    void make_reply(Connection& conn)
    {
        test::SimpleClientMessage msg;
        msg.set_timestamp(reply_timestamp);
        Module::send_message(conn, msg);
    }

    template <typename Connection>
    void onMessage(Connection& conn, test::SimpleClientMessage& msg)
    {
        Module::call_module(*this)
        .async_wait(std::chrono::microseconds(1),
                    [this, &conn]()
        {
            make_reply(conn);
        });
    }

    int reply_timestamp = 778899;
};

BOOST_AUTO_TEST_CASE(component_talks_to_server)
{
    struct MyServer :
        public module_component <
        MyServer,
        meta::complete_protocol<test::SimpleClientMessage>,
        Component6
        > { };


    Runner<MyServer> server;
    server.run_in_background(4999);

    Client client;
    client.connect(4999);
    server->send_message(client, testMessage);
    auto message = client.wait_message<test::SimpleClientMessage>();

    BOOST_CHECK_EQUAL(778899, message.timestamp());
}

BOOST_AUTO_TEST_CASE(loads_configuration)
{
    struct MyServer :
        public module_component <
        MyServer,
        meta::complete_protocol<test::SimpleClientMessage>,
        Component3
        > { };


    Runner<MyServer> server;
    server.run_in_background(4999);
    server.join();

    BOOST_CHECK(server->query_component<Component3>().configured.load());
}

template <class Module>
struct Component7
{
    int onMessage(int) // NOLINT(readability/casting)
    {
        return 778899;
    }
};

template <class Module>
struct Component8
{
    template <class Connection>
    void onMessage(Connection& conn, test::SimpleClientMessage&)
    {
        auto ts = Module::post_component(this, 123);
        test::SimpleClientMessage msg;
        msg.set_timestamp(ts);
        Module::send_message(conn, msg);
    }
};

template <class Module> struct Component9 { };
template <class Module> struct ComponentA { };
template <class Module> struct ComponentB { };

BOOST_AUTO_TEST_CASE(posts_componenent_message)
{
    struct MyServer :
        public module_component <
        MyServer,
        meta::complete_protocol<test::SimpleClientMessage>,
        ComponentA,
        ComponentB,
        Component7,
        Component8
        > { };


    Runner<MyServer> server;
    server.run_in_background(4999);

    Client client;
    client.connect(4999);
    server->send_message(client, testMessage);
    auto message = client.wait_message<test::SimpleClientMessage>();

    BOOST_CHECK_EQUAL(778899, message.timestamp());
}

template <class Module>
struct ComponentC
{
    test::SimpleClientMessage onMessage(test::SimpleClientMessage& msg)
    {
        return msg;
    }
};

BOOST_AUTO_TEST_CASE(implicitly_forwards_component_reply)
{
    struct Server : public module_component<Server, TestProtocol, ComponentC> { };

    Runner<Server> server;
    server.run_in_background(4999);

    Client client;
    client.connect(4999);
    server->send_message(client, testMessage);
    auto message = client.wait_message<test::SimpleClientMessage>();

    BOOST_CHECK_EQUAL(TEST_TIMESTAMP, message.timestamp());
}

BOOST_AUTO_TEST_SUITE_END()
