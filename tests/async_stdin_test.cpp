#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

#include "async_stdin.hpp"

using protoserv::async_stdin;
using protoserv::command;

BOOST_AUTO_TEST_SUITE(async_stdinTest)

BOOST_AUTO_TEST_CASE(reads_command_from_input_stream)
{
    std::istringstream stream("command\n");

    boost::asio::io_service service;
    async_stdin input(service);

    command cmd;
    input.async_read(stream, [&](auto & c)
    {
        cmd = c;
    });
    service.run();

    BOOST_CHECK_EQUAL("command", cmd.name());
}

BOOST_AUTO_TEST_CASE(reads_several_commands)
{
    std::istringstream stream("command1\r\ncommand2\r\n");

    boost::asio::io_service service;
    async_stdin input(service);

    command cmd1;
    command cmd2;

    input.async_read(stream, [&](auto & cmd)
    {
        cmd1 = cmd;
    });

    input.async_read(stream, [&](auto & cmd)
    {
        cmd2 = cmd;
    });


    service.run();

    BOOST_CHECK_EQUAL("command1", cmd1.name());
    BOOST_CHECK_EQUAL("command2", cmd2.name());
}

BOOST_AUTO_TEST_CASE(dispatches_only_one_event)
{
    std::istringstream stream("command1\r\ncommand2\r\n");

    boost::asio::io_service service;
    async_stdin input(service);

    int called = 0;
    input.async_read(stream, [&](auto & cmd)
    {
        ++called;
    });

    service.run();

    BOOST_CHECK_EQUAL(1, called);
}

BOOST_AUTO_TEST_CASE(handles_event_in_the_asio_service_thread)
{
    std::istringstream stream("command1\r\n");

    boost::asio::io_service service;
    async_stdin input(service);

    std::thread::id handler_thread_id;
    input.async_read(stream, [&](auto & cmd)
    {
        handler_thread_id = std::this_thread::get_id();
    });

    service.run();

    BOOST_CHECK_EQUAL(std::this_thread::get_id(), handler_thread_id);
}

BOOST_AUTO_TEST_CASE(cleanly_destroys_object)
{
    boost::asio::io_service service;
    {
        std::istringstream stream("command1\r\n");
        async_stdin input(service);

        std::thread::id handler_thread_id;
        input.async_read(stream, [&](auto & cmd) { });

    }
    service.run();
}

BOOST_AUTO_TEST_SUITE_END()
