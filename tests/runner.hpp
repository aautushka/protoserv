#pragma once
#include <thread>
#include <string>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

namespace tests
{
namespace fs = boost::filesystem;

template <typename Server>
class Runner
{
public:
    ~Runner()
    {
        join();
    }

    void join()
    {
        server_.set_active(false);

        if (background_worker_.joinable())
        {
            background_worker_.join();
        }
    }

    void run_in_background(protoserv::Options& conf)
    {
        auto srv = &server_;

        background_worker_ = std::thread
        {
            [conf, srv]()
            {
                srv->run_server("magic", conf);
            }
        };
    }

    void run_in_background(uint16_t port)
    {
        using protoserv::Options;
        Options conf;
        conf["Port"] = boost::lexical_cast<std::string>(port);
        run_in_background(conf);
    }

    void run(protoserv::Options& conf)
    {
        server_.run_server("magic", conf);
    }

    void run(uint16_t port)
    {
        protoserv::Options conf;
        conf["Port"] = boost::lexical_cast<std::string>(port);
        run(conf);
    }

    void wait()
    {
        background_worker_.join();
    }

    void configure(const fs::path& configPath)
    {
        config_path_ = configPath;
    }

    Server* operator ->()
    {
        return &server_;
    }

private:

    fs::path config_path_;
    std::thread background_worker_;
    Server server_;
};

} // namespace tests

