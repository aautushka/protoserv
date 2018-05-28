#define __STDC_WANT_LIB_EXT1__ 1

#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>
#include <boost/scoped_array.hpp>

#include <memory>
#include <iostream>
#include <string>
#include <iomanip>

#include <string.h>

#include "basic_session.hpp"
#include "message.hpp"

#include "protobuf_messages/messages.pb.h"

using tests::Type6Message;

class ProtobufFormatter : public protoserv::protobuf_packet<ProtobufFormatter>
{
public:
    boost::scoped_array<char> buffer;
    boost::scoped_array<char> read_buffer;
    size_t buffer_length;
    size_t read_buffer_length;
    int msg_type;

    ProtobufFormatter() : buffer_length(0), msg_type(0), read_buffer_length(0)
    {}

    using protoserv::protobuf_packet<ProtobufFormatter>::send;
    using protoserv::protobuf_packet<ProtobufFormatter>::handle_message;

    void send(const void* buf, size_t len)
    {
        buffer_length = len;
        buffer.reset(new char[buffer_length]);
        memcpy(buffer.get(), buf, buffer_length);
    }

    void notify_message(const protoserv::Message& message)
    {
        msg_type = message.type;
        BOOST_CHECK(message.size >= 0);
        read_buffer_length = message.size;
        read_buffer.reset(new char[read_buffer_length]);
        memcpy(read_buffer.get(), message.data, read_buffer_length);
    }
};

struct Fixture {};

BOOST_FIXTURE_TEST_SUITE(packet_formatting, Fixture)

BOOST_AUTO_TEST_CASE(protobuf_simple_message)
{
    ProtobufFormatter formatter;
    constexpr int in_message_type = 333;
    constexpr size_t size_1_field = sizeof(uint16_t);
    constexpr size_t size_2_field = sizeof(uint16_t);

    Type6Message request;
    request.set_data("Hello world!");
    //1. send
    formatter.send(in_message_type, request);

    BOOST_CHECK(formatter.buffer);
    auto ptr = reinterpret_cast<uint16_t*>(formatter.buffer.get());
    auto buffer_size = ptr[0];
    auto out_message_type = ptr[1];

    BOOST_CHECK_EQUAL(formatter.buffer_length, buffer_size);
    BOOST_CHECK_EQUAL(in_message_type, out_message_type);

    Type6Message response;
    BOOST_CHECK(response.ParseFromArray(&ptr[2], static_cast<int>(formatter.buffer_length) - size_1_field - size_2_field));
    BOOST_CHECK_EQUAL(request.data(), response.data());
    response.Clear();

    //2. receive
    formatter.handle_message(ptr);

    BOOST_CHECK(formatter.read_buffer);
    ptr = reinterpret_cast<uint16_t*>(formatter.read_buffer.get());

    BOOST_CHECK_EQUAL(formatter.read_buffer_length, buffer_size - size_1_field - size_2_field);
    BOOST_CHECK_EQUAL(in_message_type, formatter.msg_type);
    BOOST_CHECK(response.ParseFromArray(formatter.read_buffer.get(), static_cast<int>(formatter.read_buffer_length)));
    BOOST_CHECK_EQUAL(request.data(), response.data());
}

BOOST_AUTO_TEST_CASE(protobuf_empty_message)
{
    ProtobufFormatter formatter;
    constexpr int in_message_type = 333;
    constexpr size_t size_1_field = sizeof(uint16_t);
    constexpr size_t size_2_field = sizeof(uint16_t);

    Type6Message request;
    Type6Message response;
    formatter.send(in_message_type, request);
    BOOST_CHECK(formatter.buffer);
    auto ptr = reinterpret_cast<uint16_t*>(formatter.buffer.get());
    auto buffer_size = ptr[0];
    auto out_message_type = ptr[1];

    //just check that we send only header
    BOOST_CHECK_EQUAL(size_1_field + size_2_field, formatter.buffer_length);

    BOOST_CHECK_EQUAL(formatter.buffer_length, buffer_size);
    BOOST_CHECK_EQUAL(in_message_type, out_message_type);

    BOOST_CHECK(response.ParseFromArray(&ptr[2], static_cast<int>(formatter.buffer_length) - size_1_field - size_2_field));

    BOOST_CHECK_EQUAL(request.data(), response.data());

    //2. receive
    formatter.handle_message(ptr);

    BOOST_CHECK(formatter.read_buffer);
    ptr = reinterpret_cast<uint16_t*>(formatter.read_buffer.get());

    BOOST_CHECK_EQUAL(0, formatter.read_buffer_length); // empty protobuf message
    BOOST_CHECK_EQUAL(formatter.read_buffer_length, buffer_size - size_1_field - size_2_field);
    BOOST_CHECK_EQUAL(in_message_type, formatter.msg_type);

    BOOST_CHECK(response.ParseFromArray(formatter.read_buffer.get(), static_cast<int>(formatter.read_buffer_length)));
    BOOST_CHECK_EQUAL(request.data(), response.data());
}

BOOST_AUTO_TEST_SUITE_END()
