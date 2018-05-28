#pragma once
#include "message.hpp"
#include <memory.h>
#include <malloc.h>
#include <algorithm>

namespace protoserv
{
/*
@description
Buffer which stores both the Message structure and the message bytes.
Follows the unique_ptr paradigm, a smart pointer of sorts: moveable but not copyable
*/
class messagebuf
{
public:

    messagebuf() noexcept
    {
    }

    // @brief frees the memory
    ~messagebuf()
    {
        free(msg_);
    }

    // @brief copy ctor and copy asignment are not allowed
    messagebuf(const messagebuf&) = delete;
    messagebuf& operator =(const messagebuf&) = delete;

    // @brief Moves another buffer
    messagebuf(messagebuf&& rhs) noexcept
    {
        msg_ = rhs.msg_;
        rhs.msg_ = nullptr;
    }

    // @brief Move assignes anothe rbuffer
    messagebuf& operator =(messagebuf&& rhs) noexcept
    {
        free(msg_);
        msg_ = rhs.msg_;
        rhs.msg_ = nullptr;
        return *this;
    }

    // @brief Allocates memory and copies the give message
    static messagebuf copy(const Message& msg)
    {
        auto m = static_cast<Message*>(malloc(sizeof(Message) + msg.size));
        memcpy(m, &msg, sizeof(Message));
        memcpy(m + 1, msg.data, msg.size);
        m->data = m + 1;

        messagebuf ret;
        ret.msg_ = m;
        return ret;
    }

    // @brief Returns the stored mesasge
    Message* get() noexcept
    {
        return msg_;
    }

    // @brief Returns the stored message
    Message& operator *()
    {
        return *get();
    }

    // @brief Returns the stored message
    Message* operator-> () noexcept
    {
        return get();
    }
private:
    Message* msg_ = nullptr;
};

} // namespace protoserv
