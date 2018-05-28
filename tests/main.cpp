#define BOOST_TEST_MODULE protoserv tests
#include <boost/test/unit_test.hpp>
#include <boost/test/debug.hpp>

#include <google/protobuf/service.h>

struct GlobalFixture
{
    GlobalFixture()
    {
        boost::debug::detect_memory_leaks(false);
        boost::unit_test::unit_test_log.set_threshold_level(boost::unit_test::log_level::log_messages);
    }

    ~GlobalFixture()
    {
        google::protobuf::ShutdownProtobufLibrary();
    }
};

BOOST_GLOBAL_FIXTURE(GlobalFixture);

//custom entry point can be written here
