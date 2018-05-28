#pragma once

#include "boost/asio.hpp"
#include "boost/algorithm/string.hpp"

#include <thread>
#include <queue>
#include <mutex>
#include <iostream>
#include <utility>
#include <string>
#include <vector>


namespace protoserv
{

// @brief Represents stdin command
class command
{
public:
    // @brief Returns command name
    const std::string& name() const
    {
        return name_;
    }

    // @brief Returns the number of command arguments
    size_t argc() const
    {
        return args_.size();
    }

    // @brief Returns the argument by its index
    const std::string& arg(size_t index) const
    {
        return args_[index];
    }

    // @brief Return complete command string
    const std::string& raw() const
    {
        return raw_;
    }

    // @brief Parses and stores stdin command
    void parse(const std::string& raw_str)
    {
        raw_ = raw_str;
        boost::algorithm::split(args_, raw_str, boost::is_any_of("\t "));

        if (!args_.empty())
        {
            name_ = args_.front();
            args_.erase(args_.begin());
        }
    }

private:
    std::vector<std::string> args_;
    std::string name_;
    std::string raw_;
};

// @brief Asynchronous stdin reader
// @description
// The asynchronous capability is achieved by direct calls to Win32 API.
// Runs a separated thread to monitor the stream.
class async_stdin
{
public:
    using handler_type = std::function<void(const command&)>;

    explicit async_stdin(boost::asio::io_service& service)
        : service_(service)
        , timer_(service)
        , terminate_(false)
        , dispatch_count_(0)
    {
        keep_alive();
    }

    ~async_stdin()
    {
        stop();
    }

    // @brief Reads the stream, calls the handler when well formed command is received
    void async_read(std::istream& input, const handler_type& handler)
    {
        if (!input.eof())
        {
            if (!worker_.joinable())
            {
                worker_ = std::thread{ [this]()
                {
                    read_istream();
                } };
            }

            keep_alive();

            push_queue(input, handler);
        }
    }


private:
    using request_type = std::pair<std::istream*, handler_type>;
    using queue = std::queue<request_type>;

    // @brief Terminates worker thread
    void stop()
    {
        terminate_ = true;
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    // @brief Keeps the asio service alive
    void keep_alive()
    {
        timer_.expires_from_now(boost::posix_time::milliseconds(100));
        timer_.async_wait(
            [this](boost::system::error_code err)
        {
            if (!err)
            {
                if (!terminate_ && !is_empty_queue())
                {
                    keep_alive();
                }
            }
        }
        );
    }

    // @brief Pushes new command on the command queue
    void push_queue(std::istream& input, const handler_type& handler)
    {
        ++dispatch_count_;
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace(&input, handler);
    }

    // @brief Pops a command from the queue if any
    bool pop_queue(request_type& request)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!queue_.empty())
        {
            request = queue_.front();
            queue_.pop();
            return true;
        }
        return false;
    }

    // @brief Checks if mesasge queue is empty
    bool is_empty_queue()
    {
        return dispatch_count_ == 0;
    }

    // @brief Reads command from the stream
    void read_istream()
    {
        request_type request;
        std::string raw_input;
        std::string str_cmd;
        while (!terminate_)
        {
            if (pop_queue(request))
            {
                auto& input = *request.first;
                while (!terminate_)
                {
                    read_some(raw_input, input);
                    auto newline = raw_input.find("\n");

                    if (newline != std::string::npos)
                    {
                        str_cmd.assign(raw_input, 0, newline);
                        raw_input.erase(0, newline + 1);
                        boost::algorithm::trim(str_cmd);

                        command c;
                        c.parse(str_cmd);

                        service_.dispatch(
                            [this, cmd{ std::move(c) }, handler{request.second}]()
                        {
                            handler(cmd);
                            --dispatch_count_;
                        });

                        break;
                    }
                    else
                    {
                        std::this_thread::yield();
                    }
                }
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }

    // @brief Check is character is printable ASKII
    static bool is_printable_askii(char ch)
    {
        return ch >= 0x20 && ch <= 0x7e;
    }

    // @brief Reads some characters from iostream, does not really work on windows
    static bool read_some_iostream(std::string& input, std::istream& stream)
    {
        char buf[128];
        auto read = stream.readsome(buf, sizeof(buf));

        if (read > 0)
        {
            input.append(buf, buf + read);
            return true;
        }

        return false;
    }

    // @brief Reads some characters from iostream
    static void read_some(std::string& inputbuf, std::istream& stream)
    {
        if (!read_some_iostream(inputbuf, stream))
        {
        }
    }

    boost::asio::io_service& service_;
    boost::asio::deadline_timer timer_;
    std::atomic<bool> terminate_;
    std::atomic<int> dispatch_count_;
    std::thread worker_;

    queue queue_;
    std::mutex mutex_;
};

} // namespace protoserv
