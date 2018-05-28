
#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <chrono>

namespace protoserv
{
// @brief Asynchronous timer
class Timer : public std::enable_shared_from_this<Timer>
{
public:
    using pointer = std::shared_ptr<Timer>;

    explicit Timer(boost::asio::io_service& svc)
        : timer_(svc)
        , period_(0)
    {
    }

    // @brief Schedules recurring asynchronous event
    // @description
    // Period is of std::chrono family (e.g std::chrono::microsecond)
    template <typename Period>
    void start(Period period, std::function<void(void)> handler)
    {
        auto microsec = std::chrono::duration_cast<std::chrono::microseconds>(period);
        period_ = boost::posix_time::microsec{ microsec.count() };
        handler_ = handler;
        resume();
    }

    // @brief Pauses the timer, no more handler are going to be called
    void stop()
    {
        pause();
    }

    // @brief Parses the timer, same as stop()
    void pause()
    {
        paused_ = true;
    }

    // @brief Resumes the previously paused timer
    void resume()
    {
        paused_ = false;
        timer_.expires_from_now(period_);
        do_async_wait();
    }

private:

    // @brief Schedules async timer event
    void do_async_wait()
    {
        // asio keeps a reference to the timer thus preventing it from being destroyed
        auto self = shared_from_this();
        timer_.async_wait([this, self](boost::system::error_code err)
        {
            if (!err && !paused_)
            {
                handler_();

                // re-schedule timer event if not paused
                if (!paused_)
                {
                    resume();
                }
            }
        });
    }

    boost::asio::deadline_timer timer_;
    boost::posix_time::microseconds period_;
    std::function<void(void)> handler_;
    bool paused_ = false;
};
} // namespace protoserv
