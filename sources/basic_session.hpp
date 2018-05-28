#pragma once

#include "session_buffers.hpp"
#include "message.hpp"

#include <google/protobuf/message.h>

#include <boost/container/small_vector.hpp>
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <iostream>

#include <chrono>
#include <limits>
#include <string>
#include <any>

namespace protoserv
{
/*
@class protobuf_packet
@description
Protobuf <Message> format
--------------------------------------
|message type |  protobuf message    |
--------------------------------------
|  uint16_t   |        binary        |
--------------------------------------
*/
template<typename Derived>
class protobuf_packet
{
public:
    void send(int messageType, const google::protobuf::Message& msg)
    {
        constexpr auto header_size = 2 * sizeof(uint16_t);
        const auto message_size = msg.ByteSize();
        const auto size = header_size + message_size;
        auto buf = static_cast<uint16_t*>(alloca(size));
        assert(size <= std::numeric_limits<uint16_t>::max());
        buf[0] = static_cast<uint16_t>(size);
        buf[1] = messageType;
        msg.SerializePartialToArray(&buf[2], message_size);

        static_cast<Derived*>(this)->send(buf, size);
    }

    void handle_message(uint16_t* header)
    {
        Message msg{ 0 };
        msg.type = header[1];
        msg.size = header[0] - 4;
        msg.data = &header[2];

        static_cast<Derived*>(this)->notify_message(msg);
    }
};

/*
@class basic_session
@brief Low-level session operations
@description

Network packet format
------------------------------
|buffer size|   <Message>    |
------------------------------
|  uint16_t |     binary     |
------------------------------

<Message> support 2 formats:
- Protobuf <Message> format
*/
template <
    class Derived,
    typename Formatter = protobuf_packet<Derived>
    >
class basic_session: public Formatter
{
public:
    using clock_type = std::chrono::steady_clock;
    using tcp = boost::asio::ip::tcp;
    using Formatter::send;
    using Formatter::handle_message;

    explicit basic_session(tcp::socket socket)
        : socket_(std::move(socket))
    {
        readbuf_.reserve(2 * 1024);
    }

    void connect(tcp::socket&& socket)
    {
        socket_ = std::move(socket);
        set_connected();
    }

    basic_session(const basic_session&) = delete;
    basic_session& operator =(const basic_session&) = delete;

    ~basic_session()
    {
    }

    /*
    @description
    Forces session termination.  The session is not supposed to be
    used after calling this function! Does not notify the derivee class
    about session being terminated;
    */
    void kill()
    {
        boost::system::error_code ec;
        socket_.close(ec);
        connected_ = false;
    }

    /*
    @description
    Closes a previously open session, notifies all interested parties.
    */
    void close()
    {
        if (connected_)
        {
            orderly_disconnect();
        }
    }

    /*
    @description
    Shut down any communicatoin on the socket. The socket remain in "open" state.
    */
    void shutdown()
    {
        boost::system::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
    }

    /*
    @description
    Initiates IO on the server socket. Perhaps, this method does not belong here.
    The initiated IO includes both write and read operations.
    */
    void start()
    {
        set_connected();
        do_read_recurring();
        do_write();
    }

    /*
    @description
    Check if socket is connected or not, soft of. It does not perform
    any network operation, and it does not know the actual state of the connection
    */
    bool connected() const
    {
        return connected_;
    }

    /*
    @description
    Asynchronously connects to the given endpoint. Calls the handler when successful
    or error occurs.
    */
    void async_connect(tcp::endpoint endpoint, std::function<void(boost::system::error_code)> handler)
    {
        socket_.async_connect(endpoint, handler);
    }

    /*
    @description
    A session has an assosicated last activity timeout. This function would
    close the session (with all notifications) if the timeout expires.
    Expects a std::chrono family parameter (e.g. std::chrono::milliseconds{10})
    */
    template <typename Duration>
    void disconnect_inactive(Duration duration)
    {
        auto inactivity = clock_type::now() - last_activity_;
        if (inactivity > duration)
        {
            orderly_disconnect();
        }
    }

    /*
    @description
    Reads the socket, the number of bytes read is unknown and depends on the network, the peer,
    the OS etc. Once read, tries to parse the data and fire notifications.
    */
    void read_some()
    {
        struct read_once
        {
            explicit read_once(basic_session& s) : session(s) { }
            ~read_once()
            {
                session.readbuf_.grow_capacity();
            }
            basic_session& session;
        };
        do_read<read_once>();
    }

    /*
    @description
    Associates some user data with the connection. The user data may
    be stored in-place, along with the session object inself, but this
    depends on sizeof(T)
    */
    template <typename T>
    void set_user_data(T&& t)
    {
        user_ = std::forward<T>(t);
    }

    /*
    @description
    Returns session user data previously set by set_user_data.
    Make sure you pass the same type in the template paramter.
    */
    template <typename T>
    T& get_user_data()
    {
        return *std::any_cast<T>(&user_);
    }

    /*
    @description
    Tries to obtain the associated user data. Returns nullptr if
    not matching type is requested.
    */
    template <typename T>
    T* get_user_data_if()
    {
        if (user_.has_value())
        {
            return std::any_cast<T>(&user_);
        }
        return nullptr;
    }

private:

    /*
    @description
    Changes the state of the session to "connected", fires a notificatoin
    */
    void set_connected()
    {
        assert(socket_.is_open());
        connected_ = true;
        static_cast<Derived*>(this)->notify_connected();
        refresh_activity();
    }


    /*
    @description
    Schedules an async write operation, copies the data to the internal write buffer first
    */
    void send(const void* buf, size_t len)
    {
        if (connected_)
        {
            writebuf_.append(buf, len);
            if (!write_in_progress_)
            {
                do_write();
            }
        }
    }

    /*
    @description
    The class schedules an async read operation on the given session.
    This could be a one-chance operation, or a series of recurrings reads.
    */
    class read_scheduler
    {
    public:
        explicit read_scheduler(basic_session& session)
            : session_(session)
            , postpone_(!session.readbuf_.free_capacity())
        {
            if (!postpone_)
            {
                session_.do_read<read_scheduler>();
            }
        }

        ~read_scheduler()
        {
            if (postpone_)
            {
                session_.readbuf_.grow_capacity();
                session_.do_read<read_scheduler>();
            }
        }

    private:
        basic_session& session_;
        bool postpone_ = false;
    };

    /*
    @description
    Schedules a recurring read operation, meaning if one operation ends,
    the next one gets scheduled at once
    */
    void do_read_recurring()
    {
        do_read<read_scheduler>();
    }

    /*
    @description
    Schedules an async read operation
    */
    template <typename Scheduler>
    void do_read()
    {
        assert(readbuf_.free_capacity() > 0);

        schedule_operation();

        socket_.async_read_some(get_read_buffer(), [this](auto err, size_t len)
        {
            complete_operation();

            if (!err)
            {
                readbuf_.grow(len);
                Scheduler scheduler(*this);
                process_read_data();
            }
            else
            {
                orderly_disconnect();
            }
        });
    }

    /*
    @description
    Wraps the free part of the read buffer in an asio structere, for asio to
    be able to consume it.
    */
    auto get_read_buffer()
    {
        assert(readbuf_.free_capacity() > 0);
        return boost::asio::buffer(readbuf_.end(), readbuf_.free_capacity());
    }

    /*
    @description
    Parses the read buffer and fires notifications on protobuf messages if any parsed.
    */
    void process_read_data()
    {
        parse_read_buffer();
        refresh_activity();
    }

    /*
    @description
    Schedules an async write operation. If another write operation is in flight,
    then does nothing, the re-scheduling would be performed by the other operation.
    */
    void do_write()
    {
        if (!writebuf_.empty())
        {
            auto& buf = writebuf_.flip();
            write_in_progress_.store(true);
            do_write(buf);
        }
        else
        {
            write_in_progress_.store(false);
        }
    }

    /*
    @description
    Asks asio to perform an async write operation on the given write buffer
    */
    void do_write(writebuf& buf)
    {
        using boost::system::error_code;
        boost::container::small_vector<boost::asio::const_buffer, 8> asioBuf;

        // wrap the write buffer with asio buffer
        buf.foreach([&asioBuf](auto & b)
        {
            asioBuf.emplace_back(b.begin(), b.size());
        });

        schedule_operation();

        boost::asio::async_write(socket_, asioBuf,
                                 [this, &buf](error_code err, size_t bytesTransfered)
        {
            complete_operation();

            if (err)
            {
                // we got an error, close the session and fire the notification
                orderly_disconnect();
            }
            else
            {

                buf.clear();

                if (writebuf_.empty())
                {
                    // no more data in the write buffer
                    write_in_progress_.store(false);
                }
                else
                {
                    // schedule another write if the write buffer has any data
                    do_write(writebuf_.flip());
                }
            }
        });
    }

    /*
    @description
    Parse the read buffer.
    */
    void parse_read_buffer()
    {
        auto buf = readbuf_.begin();
        auto beg = buf;
        auto end = readbuf_.end();

        // unless we receive 4 bytes, we can't parse the message header
        while (end - buf >= 4)
        {
            auto* msghead = reinterpret_cast<uint16_t*>(buf);
            auto messageSize = msghead[0];

            if (messageSize <= end - buf)
            {
                handle_message(msghead);
                buf += messageSize;
            }
            else
            {
                break;
            }
        }

        readbuf_.erase(buf - beg);
    }

    /*
    @description
    Close the session and notifies the listener.
    */
    void orderly_disconnect()
    {
        if (connected())
        {
            socket_.close();
            connected_.store(false);
            static_cast<Derived*>(this)->notify_disconnected();
        }

        if (!outstanding_ops_)
        {
            static_cast<Derived*>(this)->handle_disconnected_session();
        }
    }

    /*
    @description
    Updates the last activity timestamp on the session.
    */
    void refresh_activity()
    {
        last_activity_ = clock_type::now();
    }

    /*
    @description
    Increments the outstanding operation count. The non-zero counter means
    there are scheduled operations inside of asio and we can't free the
    resources just yes.
    */
    void schedule_operation()
    {
        ++outstanding_ops_;
    }

    /*
    @description
    Decrements the outstanding operation counter. When zero, the session
    may be safely destroyed.
    */
    void complete_operation()
    {
        assert(outstanding_ops_ > 0);
        --outstanding_ops_;
    }

    tcp::socket socket_;

    std::atomic_bool write_in_progress_ = false;
    std::atomic_bool connected_ = false;
    int outstanding_ops_ = 0;
    clock_type::time_point last_activity_;

    rolling_buffer readbuf_;
    double_writebuf writebuf_;

    tcp::endpoint remote_endpoint_;
    std::any user_;
};


} // namespace protoserv
