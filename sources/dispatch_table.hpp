#pragma once

#include "meta.hpp"
#include "message.hpp"

#include <boost/asio.hpp>

#include <memory>
#include <functional>
#include <cassert>
#include <list>

namespace protoserv
{
// @brief Protobuf message dispatch table
// @description
// A user of this class may subscribe to the desired message type by
// providing a callback function. The callback would be called at some
// later time, when the message is ready.:w
template <class ... Protocol>
class dispatch_table;

// @brief Empty template pack specialization
// @description
// Maintains the count of subscribed handlers
template <>
class dispatch_table <>
{
public:

    // @brief Checks if there are no pending subscribers left
    bool done() const
    {
        return 0 == pending_;
    }

    void cancel()
    {
    }

    void subscribe();
    bool dispatch();

protected:
    // @brief Increases the number of subscribers
    void add_pending_handler()
    {
        ++pending_;
    }

    // @brief Decreases the number of subsribers
    void remove_pending_handler()
    {
        assert(pending_ > 0);
        --pending_;
    }

private:
    int pending_ = 0;
};

// @brief The actual dispatch table implementation
template <class P, class ... Ps>
class dispatch_table<P, Ps...> : public dispatch_table<Ps...>
{
public:
    using base_type = dispatch_table<Ps...>;
    using base_type::dispatch;
    using base_type::subscribe;

    // @brief Subscribes to protobuf message
    void subscribe(const std::function<void(P&, boost::system::error_code)>& h)
    {
        queue_.emplace_back(h);
        dispatch_table<>::add_pending_handler();
    }

    // @brief Subscribes to protobuf message
    void subscribe(std::function<void(P&, boost::system::error_code)>&& h)
    {
        queue_.emplace_back(h);
        dispatch_table<>::add_pending_handler();
    }

    // @brief Clears subscriptions, calls all handlers with error code
    void cancel()
    {
        auto q = std::move(queue_);
        for (auto& handler : q)
        {
            P empty_message;
            using boost::system::error_code;
            auto err = error_code{ 1, boost::system::generic_category() };
            handler(empty_message, err);
        }
        queue_.clear();

        dispatch_table<Ps...>::cancel();
    }

    // @brief Calls the appropriate message handler if any
    // @description
    // Removes the handler once called, the user of this class should subscribe again
    bool dispatch(const Message& m, boost::system::error_code& err, meta::tag<P>)
    {
        if (!queue_.empty())
        {
            auto handler = pop_handler();

            P message;
            message.ParseFromArray(m.data, m.size);
            handler(message, err);
            return true;
        }

        return false;
    }

    // @brief Calls the appropriate message handler if any
    bool dispatch(P& message, boost::system::error_code err = boost::system::error_code())
    {
        if (!queue_.empty())
        {
            auto handler = pop_handler();
            handler(message, err);
            return true;
        }

        return false;
    }

private:

    // @brief Removes first handler from the list
    auto pop_handler()
    {
        auto h = std::move(queue_.front());
        queue_.pop_front();
        dispatch_table<>::remove_pending_handler();
        return h;
    }

    std::list<std::function<void(P&, boost::system::error_code)>> queue_;
};

// @brief A thin wrapper around dispatch_table
template <typename Protocol, typename ... Message>
struct table_dispatcher;

template<typename Protocol>
struct table_dispatcher<Protocol>
{
    template <typename Table>
    static bool dispatch(Table& table, const Message&)
    {
        // unexpected message type
        // we ignore it with no processing
        return true;
    }
};

template <typename Protocol, class T, class... Ts>
struct table_dispatcher<Protocol, T, Ts...>
{
    template <typename Table>
    static bool dispatch(Table& table, const Message& msg)
    {
        if (meta::identify<Protocol, T>() == msg.type)
        {
            boost::system::error_code err;
            return table.dispatch(msg, err, meta::tag<T>());
        }
        else
        {
            return table_dispatcher<Protocol, Ts...>::dispatch(table, msg);
        }
    }
};
} // namespace protoserv
