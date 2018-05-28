#pragma once

#include <boost/intrusive/list.hpp>

#include <vector>
#include <array>
#include <list>
#include <algorithm>
#include <memory>
#include <string.h>


namespace protoserv
{
// @brief An intrusive-list-ready block of memory of fixed size
template <int N>
class chunkbuf : public boost::intrusive::list_base_hook<>
{
public:

    // @brief Returns pointer to the memory
    uint8_t* begin() noexcept
    {
        return buf_;
    }

    // @brief Returns pointer past the last used byte of memory
    uint8_t* end() noexcept
    {
        return buf_ + size();
    }

    // @brief Returns used memory size
    size_t size() const noexcept
    {
        return size_;
    }

    // @brief Checks if any memory in the buffer was used
    bool empty() const noexcept
    {
        return size_ == 0;
    }

    // @brief Resets the buffer, marking all its memory free and available for use
    void clear() noexcept
    {
        size_ = 0;
    }

    // @brief Returs the capacity of the buffer in bytes
    static size_t capacity() noexcept
    {
        return N;
    }

    // @brief Returns the number of free bytes in the buffer
    size_t free_capacity() const noexcept
    {
        return capacity() - size();
    }

    // @brief Appends the buffer, makes the used memory unavailable for further write
    size_t append(const void* buf, size_t len)
    {
        auto sz = std::min(free_capacity(), len);
        memcpy(end(), buf, sz);
        size_ += sz;
        return sz;
    }

private:
    size_t size_ = 0;
    uint8_t buf_[N];
};

// @brief Intrusive list of fixed size memory chunks
class writebuf
{
public:
    // @brief Initiates the list with single chunk
    writebuf() noexcept
    {
        list_.push_back(init_buf_);
    }

    // @brief Frees allocated memory
    ~writebuf()
    {
        clear();
        assert(list_.size() == 1 && &list_.front() == &init_buf_);
        list_.clear();
        free_resources(free_list_);
    }

    // @brief The list is nor copyable nor moveable
    writebuf(const writebuf&) = delete;
    writebuf& operator =(const writebuf&) = delete;
    writebuf(writebuf&&) = delete;
    writebuf& operator =(writebuf&&) = delete;

    // @brienf Appends data to the list tail, allocates new memory if needed
    void append(const void* buf, size_t len)
    {
        auto b = static_cast<const uint8_t*>(buf);
        while (len > 0)
        {
            auto appended = list_.back().append(b, len);
            if (appended)
            {
                b += appended;
                len -= appended;
                continue;
            }
            create_chunk();
        }
    }

    // @brief Checks if there are any previously written bytes on the buffer
    bool empty() const noexcept
    {
        return init_buf_.empty();
    }

    // @brief Frees all memory
    void clear() noexcept
    {
        assert(&list_.front() == &init_buf_);
        list_.front().clear();
        list_.pop_front();

        free_resources(free_list_);
        move_to_free_list(list_);

        list_.push_back(init_buf_);
    }

    // @brief Traverses the intrusive list and calls the user supplied handler
    template <typename F>
    void foreach (F && f)
    {
        for (auto& l : list_)
        {
            f(l);
        }
    }

private:
    using chunk_type = chunkbuf<1024>;
    using list_type = boost::intrusive::list<chunk_type>;

    // @brief Allocates and initializes new list node
    void create_chunk()
    {
        if (free_list_.empty())
        {
            auto* chunk = new chunk_type;
            list_.push_back(*chunk);
        }
        else
        {
            auto& chunk = free_list_.front();
            free_list_.pop_front();
            list_.push_back(chunk);
        }
    }

    // @brief Moves buffer to free list
    void move_to_free_list(list_type& list)
    {
        while (!list.empty())
        {
            auto& l = list.front();
            list.pop_front();

            l.clear();
            free_list_.push_back(l);
        }
    }

    // @brief Deallocates memory
    void free_resources(list_type& list)
    {
        while (!list.empty())
        {
            auto& l = list.front();
            list.pop_front();

            delete &l;
        }
    }

    list_type list_;
    list_type free_list_;
    chunk_type init_buf_;
};

// @brief Double write buffer
// @description
// Hold a pair of buffers, at any given moment only one of them
// may be used for writing.
class double_writebuf
{
public:
    double_writebuf() = default;

    // @brief The double_writebuf is nor copyable nor moveable
    double_writebuf(const double_writebuf&) = delete;
    double_writebuf& operator =(const double_writebuf&) = delete;
    double_writebuf(double_writebuf&&) = delete;
    double_writebuf& operator =(double_writebuf&&) = delete;

    // @brief Appends data on the currently active buffer
    void append(const void* buf, size_t len)
    {
        current().append(buf, len);
    }

    // @brief Checks whether currently active buffer has any data
    bool empty() const
    {
        return current().empty();
    }

    // @brief Clears the currently active buffer
    void clear()
    {
        return current().clear();
    }

    // @brief Makes the seconds buffer active
    writebuf& flip()
    {
        auto& ret = current();
        cur_ ^= 1;
        return ret;
    }

    // @brief Visits every node of the currently active buffer
    // @description
    // The two internal buffers are represented as intrusive lists of buffers
    template <typename F>
    void foreach (F && f)
    {
        current().foreach(std::forward<F>(f));
    }

private:
    // @brief Returns the currently active buffer
    writebuf& current()
    {
        return buf_[cur_];
    }

    // @brief Returns the currently active buffer
    const writebuf& current() const
    {
        return buf_[cur_];
    }

    writebuf buf_[2];
    int cur_ = 0;
};

// @brief A fixed size statically allocated read buffer
template <int N>
class read_buffer
{
public:
    // @brief Buffer is neither copyable nor moveable
    read_buffer() = default;
    read_buffer(const read_buffer&) = delete;
    read_buffer& operator =(const read_buffer&) = delete;
    read_buffer(read_buffer&&) = delete;
    read_buffer& operator =(read_buffer&&) = delete;

    // @brief Returns pointer to the first byte
    uint8_t* begin()
    {
        return buf_;
    }

    // @brief Returns pointer to first free byte
    uint8_t* end()
    {
        return buf_ + size_;
    }

    // @brief Returns the number of free bytes in the buffer
    size_t free_capacity() const
    {
        return N - size_;
    }

    // @brief Returns buffer capacity
    size_t capacity() const
    {
        return N;
    }

    // @brief Returns the buffer of used bytes in the buffer
    size_t size() const
    {
        return size_;
    }

    // @brief Increases the number of used bytes
    void grow(size_t delta)
    {
        size_ += delta;
    }

    // @brief Decreases the numbef of used bytes
    void shrink(size_t delta)
    {
        size_ -= delta;
    }

    // @brief Sets the number of used bytes
    void resize(size_t size)
    {
        size_ = size;
    }

    // @brief Resets the number of used bytes to zero
    void clear()
    {
        size_ = 0;
    }

    // @brief Removes the given number of bytes from the beginning
    void erase(size_t size)
    {
        assert(size <= N);
        memmove(buf_, buf_ + size, size_ - size);
        size_ -= size;
    }

    // @brief Checks if any portion of the buffer was used
    bool empty() const
    {
        return size() == 0;
    }

private:
    uint8_t buf_[N];
    size_t size_ = 0;
};

// @brief A dynamically allocated read buffer, no upper limit on memory usage
class dyn_buffer
{
public:
    // @brief Buffer is noeither copyable nor moveable
    dyn_buffer(const dyn_buffer&) = delete;
    dyn_buffer& operator =(const dyn_buffer&) = delete;
    dyn_buffer(dyn_buffer&&) = delete;
    dyn_buffer& operator =(dyn_buffer&&) = delete;

    // @brief Constructs new buffer with some capacity
    dyn_buffer()
    {
        buf_.resize(1);
    }

    // @brief Returns pointer to the first byte
    uint8_t* begin()
    {
        return &buf_[0];
    }

    // @brief Returns pointer to the last byte
    uint8_t* end()
    {
        return begin() + size_;
    }

    // @brief Returns the number of free bytes on the buffer
    size_t free_capacity() const
    {
        return capacity() - size_;
    }

    // @brief Checks if the buffer was used up
    bool full() const
    {
        return size_ == buf_.size();
    }

    // @brief Returns the buffer capacity
    size_t capacity() const
    {
        return buf_.size();
    }

    // @brief Returns the number of used bytes
    size_t size() const
    {
        return size_;
    }

    // @brief Increases the number of used bytes
    void grow(size_t delta)
    {
        size_ += delta;
    }

    // @brief Decreases the number of used bytes
    void shrink(size_t delta)
    {
        size_ -= delta;
    }

    // @brief Sets the number of used bytes, capacity unaffected
    void resize(size_t size)
    {
        size_ = size;
    }

    // @brief Allow all previously utilized memory to be re-used
    void clear()
    {
        size_ = 0;
    }

    // @brief Clears the used bytes from the beginning
    void erase(size_t size)
    {
        memmove(begin(), begin() + size, size_ - size);
        size_ -= size;
    }

    // @brief Allocates new memory if buffer was exhausted
    void grow_capacity()
    {
        if (0 == free_capacity())
        {
            buf_.resize(buf_.size() * 2);
        }
    }

    // @brief Ensures the given buffer capacity
    void reserve(size_t bytes)
    {
        if (bytes >= capacity())
        {
            buf_.resize(bytes);
        }
    }

private:
    std::vector<uint8_t> buf_;
    size_t size_ = 0;
};

// @brief Another dynamically allocated read buffer
// @description
// Implements another strategy of memory usage, works basically
// like a circular buffer
class rolling_buffer
{
public:
    // @brief Constructs buffer with at least some capacity
    rolling_buffer()
    {
        reserve(1);
    }

    // @brief Buffer is no either copyable nor moveable
    rolling_buffer(const rolling_buffer&) = delete;
    rolling_buffer& operator =(const rolling_buffer&) = delete;
    rolling_buffer(rolling_buffer&&) = delete;
    rolling_buffer& operator =(rolling_buffer&&) = delete;


    // @brief Reserves capacity
    void reserve(size_t sz)
    {
        buf_.resize(sz);
    }

    // @brief Returns pointer to the first bytes
    uint8_t* begin()
    {
        return &buf_[0] + tail_;
    }

    // @brief Returns pointer to the first free byte
    uint8_t* end()
    {
        return &buf_[0] + head_;
    }

    // @brief Returns the buffer capacity
    size_t capacity() const
    {
        return buf_.size();
    }

    // @brief Returns the number of used bytes
    size_t size() const
    {
        assert(head_ >= tail_);
        return head_ - tail_;
    }

    // @brief Returns the number of free bytes
    size_t free_capacity() const
    {
        return capacity() - head_;
    }

    // @brief Check if buffer capacity was used up
    bool full() const
    {
        throw;
    }

    // @brief Increments the number of used bytes
    void grow(size_t sz)
    {
        assert(head_ >= tail_);
        head_ += sz;
    }

    // @brief Decrements the number of used bytes
    void shrink(size_t sz)
    {
        head_ -= sz;
        assert(head_ >= tail_);
    }

    // @brief Sets the number of used bytes to zero
    void clear()
    {
        head_ = tail_ = 0;
    }

    // @brief Marks memory in the beginning of the buffer as unused
    void erase(size_t sz)
    {
        tail_ += sz;
    }

    // @brief Grows buffer capacity if needed
    void grow_capacity()
    {
        if (tail_ > 0)
        {
            compact();
        }
        else
            if (head_ == capacity())
            {
                auto prev = buf_.size();
                buf_.resize(head_); // avoid unnecessary copying
                buf_.resize(prev * 2);
            }
    }

    // @brief Moves the used memory to the beginning of the buffer
    void compact()
    {
        memmove(&buf_[0], &buf_[0] + tail_, head_ - tail_);
        head_ -= tail_;
        tail_ = 0;
    }

private:
    size_t head_ = 0;
    size_t tail_ = 0;
    std::vector<uint8_t> buf_;
};

} // namespace protoserv
