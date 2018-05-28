#pragma once
#include "basic_session.hpp"

namespace protoserv
{

/*
@description
A client TCP/IP session. Performs all async IO operation on demand only.
*/
template <typename Server>
class basic_client_session : public basic_session<basic_client_session<Server>>
{
public:
    using self_type = basic_client_session<Server>;
    using tcp = boost::asio::ip::tcp;

    basic_client_session(tcp::socket socket, Server& server)
        : basic_session<basic_client_session<Server>>(std::move(socket))
        , server_(server)
    {
    }

    /*
    @description
    Passed the connected event to the server.
    */
    void notify_connected()
    {
        server_.notify_connected(*this);
    }

    /*
    @description
    Passes the disconnected event to the server.
    */
    void notify_disconnected()
    {
        server_.notify_disconnected(*this);
    }

    /*
    @description
    Passes the incoming protobuf message to the server.
    */
    void notify_message(const Message& message)
    {
        server_.notify_message(*this, message);
    }

    /*
    @description
    Removes the session from the server if there are no more
    references to it. A reference may be hold by a pending
    async operation for example (a timer perhaps)
    */
    void handle_disconnected_session()
    {
        assert(!already_disconnected_);

        already_disconnected_ = true;

        if (!refcount_)
        {
            server_.remove_session(this);
        }
    }

    /*
    @descpiton
    A simple reference counter class.
    */
    class reference
    {
    public:
        reference()
        {
        }

        // @brief Increment the refcount
        explicit reference(self_type& session)
            : session_(&session)
        {
            session_->addref();
        }

        // @brief Decrement the refcount
        ~reference()
        {
            release();
        }

        reference(const reference&) = delete;
        reference& operator =(const reference&) = delete;

        // @brief Decrement the refcount and move in another reference
        reference(reference&& rhs)
            : session_(rhs.session_)
        {
            rhs.session_ = nullptr;
        }

        // @brief Decrement the refcount and move in another reference
        reference& operator =(reference&& rhs)
        {
            release();
            session_ = rhs.session_;
            rhs.session_ = nullptr;
            return *this;
        }

        // @brief Get the referenced session.
        self_type& get()
        {
            assert(session_ != nullptr);
            assert(session_->is_valid_session());
            return *session_;
        }

        // @brief Get the references session
        self_type& operator* ()
        {
            return get();
        }

    private:
        // @brief Decrement the refcount.
        void release()
        {
            if (session_)
            {
                assert(session_->is_valid_session());
                session_->release();
            }
        }

        self_type* session_ = nullptr;
    };

    /*
    @description
    Increments the reference counter and passed the reference object to the caller.
    If the caller fails to store the object, the reference count would go down,
    which could possibly cause the session to be destroyed
    */
    reference take_ownership()
    {
        return reference{ *this };
    }

private:
    // @brief Increment the reference counter
    void addref()
    {
        ++refcount_;
    }

    // @brief Decrement the reference counter
    auto release()
    {
        assert(refcount_ > 0);
        --refcount_;

        // remove the session from the server if there are no more references
        // and the session appears to be in disconnected state
        if (!refcount_ && already_disconnected_)
        {
            server_.remove_session(this);
        }
    }

    // @brief Ask server if the current session is still valid and alive.
    bool is_valid_session() const
    {
        return server_.is_valid_session(this);
    }

    Server& server_;
    int refcount_ = 0;
    bool already_disconnected_ = false;
};
} // namespace protoserv
