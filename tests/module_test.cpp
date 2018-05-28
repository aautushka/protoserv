#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

#include "module.hpp"
#include "runner.hpp"
#include "async_client.hpp"

#include "protobuf_messages/messages.pb.h"

namespace test = tests;

template <typename T>
using Runner = test::Runner<T>;


using TestProto = meta::proto <
                  test::SimpleClientMessage,
                  test::Type1Message,
                  test::Type2Message,
                  test::Type3Message,
                  test::Type4Message,
                  test::Type5Message,
                  test::Type6Message,
                  test::Type7Message,
                  test::Type8Message,
                  test::Type9Message
                  >;
using Client = protoserv::async_client<TestProto>;

namespace
{
class MyServer : public module_base<MyServer, TestProto>
{
public:
    void onMessage(ClientConnection& conn, test::SimpleClientMessage& msg)
    {
        test::SimpleClientMessage reply;
        reply.set_timestamp(msg.timestamp() + 1);
        reply.set_payload(msg.payload());

        send_message(conn, reply);
    }
};

class EchoServer : public module_base<EchoServer, TestProto>
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

struct MyServer2 : public module_base<MyServer2, TestProto>
{
    void onMessage(ClientConnection& conn, test::Type1Message& msg)
    {
        test::Type1Message reply;
        reply.set_data(-msg.data());
        send_message(conn, reply);
    }

    void onMessage(ClientConnection& conn, test::Type2Message& msg)
    {
        test::Type2Message reply;
        reply.set_data(-msg.data());
        send_message(conn, reply);
    }

    void onMessage(ClientConnection& conn, test::Type3Message& msg)
    {
        test::Type3Message reply;
        reply.set_data(!msg.data());
        send_message(conn, reply);
    }

    void onMessage(ClientConnection& conn, test::Type4Message& msg)
    {
        test::Type4Message reply;
        reply.set_data(-msg.data());
        send_message(conn, reply);
    }

    void onMessage(ClientConnection& conn, test::Type5Message& msg)
    {
        test::Type5Message reply;
        reply.set_data(-msg.data());
        send_message(conn, reply);
    }

    void onMessage(ClientConnection& conn, test::Type6Message& msg)
    {
        test::Type6Message reply;
        auto d = msg.data();
        std::transform(d.begin(), d.end(), d.begin(), [](auto c)
        {
            return std::toupper(c);
        });
        reply.set_data(d);
        send_message(conn, reply);
    }

    void onMessage(ClientConnection& conn, test::Type7Message& msg)
    {
        test::Type7Message reply;
        auto d = msg.data();
        std::transform(d.begin(), d.end(), d.begin(), [](auto c)
        {
            return ~c;
        });
        reply.set_data(d);
        send_message(conn, reply);
    }

    void onMessage(ClientConnection& conn, test::Type8Message& msg)
    {
        test::Type8Message reply;
        reply.set_data1(-msg.data1());
        reply.set_data2(-msg.data2());
        send_message(conn, reply);
    }

    void onMessage(ClientConnection& conn, test::Type9Message& msg)
    {
        test::Type9Message reply;
        reply.set_data1(-msg.data1());
        reply.set_data2(-msg.data2());
        reply.set_data3(-msg.data3());
        send_message(conn, reply);
    }
};

template <typename Module>
struct MyServerHandler
{
    template <typename ServerConnection>
    void onMessage(ServerConnection& conn, test::SimpleClientMessage& msg)
    {
        handler(msg);
    }

    std::function<void(test::SimpleClientMessage&)> handler;
};


class MyProxy :
    public module_base<MyProxy, test::SimpleClientMessage>,
    public MyServerHandler<MyProxy>
{
public:
    using module_base::ClientConnection;
    using module_base::ServerConnection;

    MyProxy()
    {
        client_ = std::move(handle_server_sync<MyServerHandler>(*this, "127.0.0.1", 5999));
    }

    void onMessage(ClientConnection& conn, test::SimpleClientMessage& msg)
    {
        MyServerHandler::handler = [this, &conn](auto & msg)
        {
            send_message(conn, msg);
        };
        send_message(*client_, msg);
    }

    std::shared_ptr<Client<MyServerHandler>> client_;
};

class MyProxy2 : public module_base<MyProxy2, test::SimpleClientMessage>
{
public:
    MyProxy2()
    {
        server_connection_ = std::move(handle_server_sync<MyProxy2>(*this, "127.0.0.1", 5999));
    }

    void onMessage(ClientConnection& conn, test::SimpleClientMessage& msg)
    {
        send_message(*server_connection_, msg);
        client_connection_ = &conn;
    }

    void onMessage(ServerConnection& conn, test::SimpleClientMessage& msg)
    {
        send_message(*client_connection_, msg);
    }

    std::shared_ptr<Client<MyProxy2>> server_connection_;
    ClientConnection* client_connection_ = nullptr;
};

class MyProxy3 : public module_base<MyProxy3, test::SimpleClientMessage>
{
public:
    MyProxy3()
    {
        server_connection_ = &connect_to_server("127.0.0.1", 5999);
    }

    void onMessage(ClientConnection& conn, test::SimpleClientMessage& msg)
    {
        send_message(*server_connection_, msg);
        client_connection_ = &conn;
    }

    void onMessage(ServerConnection& conn, test::SimpleClientMessage& msg)
    {
        send_message(*client_connection_, msg);
    }

    ServerConnection* server_connection_ = nullptr;
    ClientConnection* client_connection_ = nullptr;
};

} // namespace anonymous

struct AppModuleFixture
{
    enum
    {
        SERVER_PORT = 5999,
        TEST_TIMESTAMP = 121212
    };

    AppModuleFixture()
    {
        testMessage.set_timestamp(TEST_TIMESTAMP);
    }

    void run_client()
    {
        Client client;
        client.connect(SERVER_PORT);
        server->send_message(client, testMessage);
    }

    void client_connect()
    {
        client.connect(SERVER_PORT);
    }

    void client_send_test_message()
    {
        server->send_message(client, testMessage);
    }

    test::SimpleClientMessage testMessage;

    // client is be terminated before server, or a leak would appear
    // the order is essential
    Runner<MyServer> server;
    Client client;
};

BOOST_FIXTURE_TEST_SUITE(AppModuleTest, AppModuleFixture)

BOOST_AUTO_TEST_CASE(receives_server_response)
{
    server.run_in_background(SERVER_PORT);

    client_connect(); // connect to port 5999
    client_send_test_message(); // send test message with TEST_TIMESTAMP

    // server responds to SimpleClientMessage with the same message type
    // incrementing the received timestamp in the meantime
    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(TEST_TIMESTAMP + 1, message.timestamp());

    client.disconnect();
}

BOOST_AUTO_TEST_CASE(connects_multiple_clients_in_succession)
{
    server.run_in_background(SERVER_PORT);

    for (auto i = 0u; i < 2 * std::thread::hardware_concurrency(); ++i)
    {
        Client client;
        client.connect(SERVER_PORT);
        server->send_message(client, testMessage);
        auto message = client.wait_message<test::SimpleClientMessage>();
        BOOST_CHECK_EQUAL(TEST_TIMESTAMP + 1, message.timestamp());
    }
}

BOOST_AUTO_TEST_CASE(connect_multiple_clients_in_parallel)
{
    server.run_in_background(SERVER_PORT);

    //Client is not copyable now
    std::vector<std::shared_ptr<Client>> clients;
    clients.reserve(2 * std::thread::hardware_concurrency());
    for (auto i = 0u; i < 2 * std::thread::hardware_concurrency(); ++i)
    {
        clients.emplace_back();
        auto& client = clients.back();
        client.reset(new Client);
        client->connect(SERVER_PORT);
    }

    for (auto& client : clients)
    {
        server->send_message(*client, testMessage);
    }

    for (auto& client : clients)
    {
        auto message = client->wait_message<test::SimpleClientMessage>();
        BOOST_CHECK_EQUAL(TEST_TIMESTAMP + 1, message.timestamp());
    }

    clients.clear();
}

BOOST_AUTO_TEST_CASE(proxies_message)
{
    server.run_in_background(SERVER_PORT);

    Runner<MyProxy> proxy;
    proxy.run_in_background(6000);

    Client client;
    client.connect(6000);
    proxy->send_message(client, testMessage);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(TEST_TIMESTAMP + 1, message.timestamp());
}

BOOST_AUTO_TEST_CASE(proxies_message_in_style)
{
    server.run_in_background(SERVER_PORT);

    Runner<MyProxy2> proxy;
    proxy.run_in_background(6000);

    Client client;
    client.connect(6000);
    proxy->send_message(client, testMessage);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(TEST_TIMESTAMP + 1, message.timestamp());
}

BOOST_AUTO_TEST_CASE(proxies_message_by_proxy_server_with_global_message_handler)
{
    server.run_in_background(SERVER_PORT);

    Runner<MyProxy3> proxy;
    proxy.run_in_background(6000);

    Client client;
    client.connect(6000);
    proxy->send_message(client, testMessage);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(TEST_TIMESTAMP + 1, message.timestamp());
}

BOOST_AUTO_TEST_CASE(talks_to_multi_message_server)
{
    Runner<MyServer2> server;
    server.run_in_background(SERVER_PORT);

    client_connect();

    test::Type1Message msg1;
    msg1.set_data(123);
    server->send_message(client, msg1);
    BOOST_CHECK_EQUAL(-123, client.wait_message<test::Type1Message>().data());

    test::Type2Message msg2;
    msg2.set_data(456);
    server->send_message(client, msg2);
    BOOST_CHECK_EQUAL(-456, client.wait_message<test::Type2Message>().data());

    test::Type3Message msg3;
    msg3.set_data(true);
    server->send_message(client, msg3);
    BOOST_CHECK_EQUAL(false, client.wait_message<test::Type3Message>().data());

    test::Type4Message msg4;
    msg4.set_data(789.0);
    server->send_message(client, msg4);
    BOOST_CHECK_EQUAL(-789.0, client.wait_message<test::Type4Message>().data());

    test::Type5Message msg5;
    msg5.set_data(-123.0);
    server->send_message(client, msg5);
    BOOST_CHECK_EQUAL(123.0, client.wait_message<test::Type5Message>().data());

    test::Type6Message msg6;
    msg6.set_data("hello world");
    server->send_message(client, msg6);
    BOOST_CHECK_EQUAL("HELLO WORLD", client.wait_message<test::Type6Message>().data());

    test::Type7Message msg7;
    msg7.set_data("\x01\xfe");
    server->send_message(client, msg7);
    BOOST_CHECK_EQUAL("\xfe\x01", client.wait_message<test::Type7Message>().data());

    test::Type8Message msg8;
    msg8.set_data1(11);
    msg8.set_data2(22);
    server->send_message(client, msg8);
    auto reply8 = client.wait_message<test::Type8Message>();
    BOOST_CHECK_EQUAL(-11, reply8.data1());
    BOOST_CHECK_EQUAL(-22, reply8.data2());

    test::Type9Message msg9;
    msg9.set_data1(33);
    msg9.set_data2(44);
    msg9.set_data3(55);
    server->send_message(client, msg9);
    auto reply9 = client.wait_message<test::Type9Message>();
    BOOST_CHECK_EQUAL(-33, reply9.data1());
    BOOST_CHECK_EQUAL(-44, reply9.data2());
    BOOST_CHECK_EQUAL(-55, reply9.data3());

    client.disconnect();
}

BOOST_AUTO_TEST_CASE(implicitly_forwards_return_value_to_client)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        test::SimpleClientMessage onMessage(test::SimpleClientMessage& msg)
        {
            return msg;
        }
    };

    Runner<Server> server;
    server.run_in_background(6000);

    Client client;
    client.connect(6000);
    server->send_message(client, testMessage);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(TEST_TIMESTAMP, message.timestamp());
}

BOOST_AUTO_TEST_CASE(implicitly_forwards_return_value_to_client_alternative_syntax)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        test::SimpleClientMessage onMessage(ClientConnection&, test::SimpleClientMessage& msg)
        {
            return msg;
        }
    };

    Runner<Server> server;
    server.run_in_background(6000);

    Client client;
    client.connect(6000);
    server->send_message(client, testMessage);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(TEST_TIMESTAMP, message.timestamp());
}

BOOST_AUTO_TEST_CASE(implicitly_forwards_return_value_to_client_alternative_syntax_2)
{
    struct Server : public module_base<Server, test::SimpleClientMessage>
    {
        test::SimpleClientMessage onMessage(ClientConnection*, test::SimpleClientMessage& msg)
        {
            return msg;
        }
    };

    Runner<Server> server;
    server.run_in_background(6000);

    Client client;
    client.connect(6000);
    server->send_message(client, testMessage);

    auto message = client.wait_message<test::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(TEST_TIMESTAMP, message.timestamp());
}

BOOST_AUTO_TEST_SUITE_END()
