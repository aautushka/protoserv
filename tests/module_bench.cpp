#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

#include "module.hpp"
#include "runner.hpp"
#include "async_client.hpp"

#include "protobuf_messages/messages.pb.h"

namespace fs = boost::filesystem;
namespace test = tests;

template <typename T>
using Runner = test::Runner<T>;
using TestProto = meta::proto<test::SimpleClientMessage>;
using Client = protoserv::async_client<TestProto>;

#ifdef _DEBUG
const auto BENCH_MESSAGES = 5000;
#else
const auto BENCH_MESSAGES = 50000;
#endif

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
} // namespace anonymous

struct AppModuleBenchFixture
{
    enum
    {
        SERVER_PORT = 5999,
        TEST_TIMESTAMP = 121212
    };

    AppModuleBenchFixture()
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

class Bandwidth
{
public:
    void start()
    {
        stopped_ = started_ = now();
    }

    void stop()
    {
        stopped_ = now();
    }

    void iterate(int iter = 1)
    {
        iterations_ += iter;
    }

    auto took_us() const
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(stopped_ - started_).count();
    }

    double band() const
    {
        return static_cast<double>(iterations_) / took_us() * 1000 * 1000 * 1000;
    }

    Bandwidth()
    {
        start();
    }

    ~Bandwidth()
    {
        stop();

        std::cout << "Bandwidth: processed " << iterations_ <<
                  " messages over " << (took_us() / 1000 / 1000) << " ms" << std::endl;

        std::cout << "Bandwidth: " << band() << " msg/sec" << std::endl;
    }

private:
    using clock = std::chrono::high_resolution_clock;
    using time_point = clock::time_point;

    static time_point now()
    {
        return std::chrono::high_resolution_clock::now();
    }

    int iterations_ = 0;

    time_point started_;
    time_point stopped_;
};


BOOST_FIXTURE_TEST_SUITE(AppModuleBench, AppModuleBenchFixture, *boost::unit_test::disabled())

BOOST_AUTO_TEST_CASE(simple_sync_bench)
{
    server.run_in_background(SERVER_PORT);

    client_connect();
    client_send_test_message();

    auto server_messages = 0;

    constexpr int SYNC_BENCH_MESSAGES = 1000;
    auto left_to_send = SYNC_BENCH_MESSAGES;

    test::SimpleClientMessage message;

    {
        Bandwidth band;
        while (server_messages < SYNC_BENCH_MESSAGES)
        {
            if (client.try_receive(message))
            {
                ++server_messages;
                band.iterate();
                BOOST_CHECK_EQUAL(TEST_TIMESTAMP + 1, message.timestamp());
            }

            if (left_to_send)
            {
                client.send(testMessage);
                --left_to_send;
            }

        }
    }
}

BOOST_AUTO_TEST_CASE(simple_async_bench)
{
    server.run_in_background(SERVER_PORT);

    client_connect();
    client_send_test_message();

    auto left_to_send = BENCH_MESSAGES - 1;
    auto server_messages = 0;

    {
        Bandwidth band;
        test::SimpleClientMessage message;
        while (server_messages < BENCH_MESSAGES)
        {
            client.wait_message(message);
            ++server_messages;
            band.iterate();
            BOOST_CHECK_EQUAL(TEST_TIMESTAMP + 1, message.timestamp());

            if (left_to_send > 0)
            {
                --left_to_send;
                client.send(testMessage);
            }
        }
    }

    client.disconnect();
}

BOOST_AUTO_TEST_CASE(async_correctness_test)
{
    Runner<EchoServer> server;
    server.run_in_background(SERVER_PORT);

    client_connect();

    {
        Bandwidth band;
        auto server_messages = 0;
        auto left_to_send = BENCH_MESSAGES;
        auto sent_message_id = 0;
        test::SimpleClientMessage message;

        while (server_messages < BENCH_MESSAGES)
        {
            if (left_to_send > 0)
            {
                --left_to_send;
                ++sent_message_id;

                test::SimpleClientMessage out_message;
                out_message.set_timestamp(sent_message_id);

                server->send_message(client, out_message);
            }

            if (client.try_receive(message))
            {
                band.iterate();
                ++server_messages;
                BOOST_CHECK_EQUAL(server_messages, message.timestamp());
            }
        }
    }

    client.disconnect();
}

BOOST_AUTO_TEST_CASE(proxy_bench)
{
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

    server.run_in_background(SERVER_PORT);

    Runner<MyProxy3> proxy;
    proxy.run_in_background(6000);

    Client client;
    client.connect(6000);

    auto server_messages = 0;
    auto left_to_send = BENCH_MESSAGES;

    {
        Bandwidth band;
        test::SimpleClientMessage message;
        while (server_messages < BENCH_MESSAGES)
        {
            if (left_to_send > 0)
            {
                client.send(message);
                --left_to_send;
            }

            if (client.try_receive(message))
            {
                band.iterate();
                ++server_messages;
            }
        }
    }
}


BOOST_AUTO_TEST_CASE(bench_1kb_messages)
{
    server.run_in_background(SERVER_PORT);

    client_connect();

    test::SimpleClientMessage message;
    message.set_payload(std::string(1024, 'x'));

    auto server_messages = 0;
    auto left_to_send = BENCH_MESSAGES;

    {
        Bandwidth band;
        while (server_messages < BENCH_MESSAGES)
        {
            if (left_to_send > 0)
            {
                --left_to_send;
                client.send(message);
            }

            if (client.try_receive(message))
            {
                band.iterate();
                ++server_messages;
                BOOST_CHECK_EQUAL(message.payload().size(), 1024);
            }

        }
    }
}

BOOST_AUTO_TEST_CASE(concurrent_clients_bench)
{
    server.run_in_background(SERVER_PORT);

    std::vector<std::thread> clients;

    auto threadCount = std::thread::hardware_concurrency();
    threadCount = threadCount > 1 ? threadCount - 1 : 1;

    for (size_t i = 0; i < threadCount; ++i)
    {
        clients.emplace_back(
            [](test::SimpleClientMessage & testMessage, Runner<MyServer>& server)
        {
            Client client;
            client.connect(5999);

            auto server_messages = 0;
            auto left_to_send = BENCH_MESSAGES;

            test::SimpleClientMessage message;
            while (server_messages < BENCH_MESSAGES)
            {
                client.send(message);

                while (left_to_send > 0)
                {
                    --left_to_send;
                    client.send(message);
                }

                if (client.try_receive(message))
                {
                    ++server_messages;
                }
            }


        }, std::ref(testMessage), std::ref(server));
    }

    Bandwidth band;
    for (auto& t : clients)
    {
        t.join();
    }
    band.iterate(BENCH_MESSAGES * threadCount);
}

template <typename Server>
class ClientSession
{
public:
    void connect(uint16_t port, Server& server)
    {
        server_ = &server;
        client_.connect(port);
    }

    void start(int messageCount)
    {
        test_message_.set_timestamp(121212);
        (*server_)->send_message(client_, test_message_);
        messages_left_to_send_ = messageCount - 1;
        messages_left_to_recv_ = messageCount;
    }

    bool transact()
    {
        if (messages_left_to_send_ > 0)
        {
            client_.send(test_message_);
            messages_left_to_send_ -= 1;
        }

        test::SimpleClientMessage response;
        while (messages_left_to_recv_ > 0 && client_.try_receive(response))
        {
            messages_left_to_recv_ -= 1;
        }

        return messages_left_to_recv_ > 0;
    }

private:
    test::SimpleClientMessage test_message_;
    Client client_;
    int messages_left_to_send_ = 0;
    int messages_left_to_recv_ = 0;
    Server* server_ = nullptr;
};

BOOST_AUTO_TEST_CASE(short_concurrent_client_sessions)
{
    server.run_in_background(SERVER_PORT);

    std::vector<std::thread> clients;

    auto threadCount = std::thread::hardware_concurrency();
    threadCount = threadCount > 1 ? threadCount - 1 : 1;

    for (size_t i = 0; i < threadCount; ++i)
    {
        clients.emplace_back(
            [](test::SimpleClientMessage & testMessage, Runner<MyServer>& server)
        {
            using Session = ClientSession<Runner<MyServer>>;

            constexpr int CLIENTS_COUNT = 10;
            std::vector<Session> sessions(CLIENTS_COUNT);
            for (auto& s : sessions)
            {
                s.connect(5999, server);
                static_assert(BENCH_MESSAGES % CLIENTS_COUNT == 0, "");
                s.start(BENCH_MESSAGES / CLIENTS_COUNT);
            }

            bool hasWorkToDo = false;
            do
            {
                hasWorkToDo = false;
                for (auto& s : sessions)
                {
                    if (s.transact())
                    {
                        hasWorkToDo = true;
                    }
                }
            }
            while (hasWorkToDo);

        }, std::ref(testMessage), std::ref(server));
    }

    Bandwidth band;
    for (auto& t : clients)
    {
        t.join();
    }
    band.iterate(BENCH_MESSAGES * threadCount);
}

BOOST_AUTO_TEST_CASE(concurrent_clients_correctness_test)
{
    Runner<EchoServer> server;
    server.run_in_background(SERVER_PORT);

    std::vector<std::shared_ptr<Client>> clients;
    std::vector<int> messageCounts;
    constexpr int CLIENT_COUNT = 16;
    clients.reserve(CLIENT_COUNT);
    for (int i = 0; i < CLIENT_COUNT; ++i)
    {
        clients.emplace_back(std::make_shared<Client>());
        clients[i]->connect(5999);
        messageCounts.push_back(0);
    }

    auto left_to_send = BENCH_MESSAGES;
    auto left_to_recv = messageCounts.size() * BENCH_MESSAGES;
    test::SimpleClientMessage outmsg;
    test::SimpleClientMessage inmsg;

    do
    {
        if (left_to_send > 0)
        {
            left_to_send--;
            outmsg.set_timestamp(BENCH_MESSAGES - left_to_send);
            for (auto& client : clients)
            {
                server->send_message(*client, outmsg);
            }
        }

        int clientid = 0;
        for (int clientid = 0; clientid < CLIENT_COUNT; ++clientid)
        {
            if (clients[clientid]->try_receive(inmsg))
            {
                left_to_recv--;
                ++messageCounts[clientid];
                BOOST_CHECK_EQUAL(messageCounts[clientid], inmsg.timestamp());
            }
        }
    }
    while (left_to_recv > 0);
}

BOOST_AUTO_TEST_SUITE_END()
