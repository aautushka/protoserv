#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

#include "async_client.hpp"
#include "module.hpp"
#include "runner.hpp"

#include "protobuf_messages/messages.pb.h"

template <typename T>
using Runner = tests::Runner<T>;

namespace
{
using EchoProtocol = meta::protocol <
                     tests::SimpleClientMessage,
                     tests::Type1Message,
                     tests::Type2Message,
                     tests::Type3Message,
                     tests::Type4Message,
                     tests::Type5Message,
                     tests::Type6Message,
                     tests::Type7Message,
                     tests::Type8Message,
                     tests::Type9Message
                     >;

struct Echo : public module_base<Echo, EchoProtocol>
{
    template <typename Message>
    void onMessage(ClientConnection& conn, Message msg)
    {
        send_message(conn, msg);
    }
};

} // namespace anonymous

struct async_client_fixture
{
    template <class ... Protocol>
    using async_client = protoserv::async_client<Protocol...>;

    using client_type = async_client<EchoProtocol>;
    client_type client;

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

BOOST_FIXTURE_TEST_SUITE(async_client_test, async_client_fixture)

BOOST_AUTO_TEST_CASE(receives_message)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    client.connect(4999);
    client.send(make_message(12345));

    auto msg = client.wait_message<tests::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(12345, msg.timestamp());
}

BOOST_AUTO_TEST_CASE(receives_multiple_messages)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    client.connect(4999);
    client.send(make_message(12345));
    client.send(make_message(67890));

    auto m1 = client.wait_message<tests::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(12345, m1.timestamp());

    auto m2 = client.wait_message<tests::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(67890, m2.timestamp());
}

BOOST_AUTO_TEST_CASE(communicates_in_synchronous_fashion)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    client.connect(4999);

    client.send(make_message(12345));
    auto m1 = client.wait_message<tests::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(12345, m1.timestamp());

    client.send(make_message(67890));
    auto m2 = client.wait_message<tests::SimpleClientMessage>();
    BOOST_CHECK_EQUAL(67890, m2.timestamp());
}

BOOST_AUTO_TEST_CASE(receives_async_message)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    client.connect(4999);
    client.send(make_message(12345));

    uint64_t timestamp = 0;
    client.receive([&timestamp](tests::SimpleClientMessage & msg, auto err)
    {
        if (!err)
        {
            timestamp = msg.timestamp();
        }
    });

    client.run();

    BOOST_CHECK_EQUAL(12345, timestamp);
}


BOOST_AUTO_TEST_CASE(receives_several_async_messages)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    client.connect(4999);
    client.send(make_message(12345));
    client.send(make_message(67890));

    uint64_t ts1 = 0;
    client.receive([&ts1](tests::SimpleClientMessage & msg, auto err)
    {
        if (!err)
        {
            ts1 = msg.timestamp();
        }
    });

    uint64_t ts2 = 0;
    client.receive([&ts2](tests::SimpleClientMessage & msg, auto err)
    {
        if (!err)
        {
            ts2 = msg.timestamp();
        }
    });

    client.run();

    BOOST_CHECK_EQUAL(12345, ts1);
    BOOST_CHECK_EQUAL(67890, ts2);
}

BOOST_AUTO_TEST_CASE(cancels_pending_event)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    client.connect(4999);

    bool canceled = false;
    client.receive([&canceled](tests::SimpleClientMessage & msg, auto err)
    {
        if (err)
        {
            canceled = true;
        }
    });

    client.disconnect();
    BOOST_CHECK(canceled);
}

BOOST_AUTO_TEST_CASE(prevents_events_from_being_rescheduled_recursively_upon_a_cancelation_request)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    client.connect(4999);

    int called_times = 0;
    client.receive([this, &called_times](tests::SimpleClientMessage & msg, auto err)
    {
        ++called_times;
        client.receive([&called_times](tests::SimpleClientMessage&, auto)
        {
            ++called_times;
        });
    });

    client.disconnect();
    BOOST_CHECK_EQUAL(1, called_times);
}

BOOST_AUTO_TEST_CASE(cancels_requests_when_destructing_object)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    bool canceled = false;

    {
        client_type client;
        client.connect(4999);
        client.receive([&canceled](tests::SimpleClientMessage & msg, auto err)
        {
            if (err)
            {
                canceled = true;
            }
        });
    }

    BOOST_CHECK(canceled);
}

BOOST_AUTO_TEST_CASE(prevents_recursion_from_happening)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    int called_times = 0;
    {
        client_type client;
        client.connect(4999);

        client.receive([&client, &called_times](tests::SimpleClientMessage & msg, auto err)
        {
            ++called_times;
            client.receive([&called_times](tests::SimpleClientMessage&, auto)
            {
                ++called_times;
            });
        });
    }

    BOOST_CHECK_EQUAL(1, called_times);
}

BOOST_AUTO_TEST_CASE(receives_messages_of_different_types)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    client.connect(4999);

    tests::Type1Message m1;
    m1.set_data(123);
    client.send(m1);

    tests::Type4Message m4;
    m4.set_data(0.1234f);
    client.send(m4);

    tests::Type6Message m6;
    m6.set_data("hello world");
    client.send(m6);

    auto r1 = client.wait_message<tests::Type1Message>();
    BOOST_CHECK_EQUAL(123, r1.data());

    auto r4 = client.wait_message<tests::Type4Message>();
    BOOST_CHECK_GT(0.000001, std::abs(0.1234 - r4.data()));

    auto r6 = client.wait_message<tests::Type6Message>();
    BOOST_CHECK_EQUAL("hello world", r6.data());
}

BOOST_AUTO_TEST_CASE(handles_messages_of_different_types_asynchronously)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    client.connect(4999);

    bool type1_received = false;
    client.receive([&type1_received](tests::Type1Message & m, auto err)
    {
        type1_received = true;
    });

    bool type2_received = false;
    client.receive([&type2_received](tests::Type2Message & m, auto err)
    {
        type2_received = true;
    });

    client.send(make_message<tests::Type1Message>(123));
    client.send(make_message<tests::Type2Message>(456));
    client.run();

    BOOST_CHECK(type1_received);
    BOOST_CHECK(type2_received);
}

BOOST_AUTO_TEST_CASE(handles_subprotocol)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    async_client<meta::subp<EchoProtocol, tests::Type6Message>> client;
    client.connect(4999);
    client.send(make_message<tests::Type6Message>("hello world"));

    auto msg = client.wait_message<tests::Type6Message>();
    BOOST_CHECK_EQUAL("hello world", msg.data());
}

BOOST_AUTO_TEST_CASE(handles_subprotocol_asynchronously)
{
    Runner<Echo> server;
    server.run_in_background(4999);

    async_client<meta::subp<EchoProtocol, tests::Type6Message>> client;
    client.connect(4999);

    std::string response;
    client.receive([&response](tests::Type6Message & msg, auto err)
    {
        response = msg.data();
    });

    client.send(make_message<tests::Type6Message>("hello world"));
    client.run();

    BOOST_CHECK_EQUAL("hello world", response.data());
}

BOOST_AUTO_TEST_CASE(async_client_correctness_test, *boost::unit_test::disabled())
{
    Runner<Echo> server;
    server.run_in_background(4999);

    async_client<EchoProtocol> client;
    client.connect(4999);

    int receive_count = 0;
    for (int i = 0; i < 1000000; ++i)
    {
        tests::SimpleClientMessage msg;
        msg.set_timestamp(i);
        client.send(msg);

        client.receive([i, &receive_count](tests::SimpleClientMessage & msg, auto err)
        {
            ++receive_count;
            BOOST_CHECK_EQUAL(i, msg.timestamp());
        });
    }

    client.run();

    BOOST_CHECK_EQUAL(1000000, receive_count);
}

BOOST_AUTO_TEST_CASE(interal_read_buffer_overflow_test, *boost::unit_test::disabled())
{
    Runner<Echo> server;
    server.run_in_background(4999);

    async_client<EchoProtocol> client;
    client.connect(4999);

    int receive_count = 0;
    for (int i = 1; i < 65520; ++i)
    {
        tests::SimpleClientMessage msg;
        msg.set_payload(std::string(i, 'x'));
        client.send(msg);

        auto reply = client.wait_message<tests::SimpleClientMessage>();
        BOOST_CHECK_EQUAL(i, msg.payload().size());
    }
}

BOOST_AUTO_TEST_SUITE_END()
