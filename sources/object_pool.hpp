#pragma once
#include <vector>
#include <memory>
#include <assert.h>

namespace protoserv
{

// @brief fixed size statically allocated object pool
template<typename T, int N>
class fixed_object_pool
{
public:
    static_assert(sizeof(T) >= sizeof(void*), "");
    static_assert(N > 0, "");

    // @brief Creates a free list
    fixed_object_pool()
    {
        for (int i = 0; i < N - 1; ++i)
        {
            buffer_[i].next = &buffer_[i + 1];
        }
        buffer_[N - 1].next = nullptr;
        free_list_ = &buffer_[0];
    }

    // @brief Calls desctructors of all allocated objects
    ~fixed_object_pool()
    {
        assert(free_list_size() == free_size_);
        destroy_all();
        assert(free_size_ == N);
    }

    // @brief Creates new object on the pool, if not enough memory returns nullptr
    template <class ... Ts>
    T* create(Ts&&... ts)
    {
        if (free_list_)
        {
            --free_size_;
            T* ret = &free_list_->obj;
            free_list_ = free_list_->next;
            new (ret) T(std::forward<Ts>(ts)...);
            return ret;
        }

        return nullptr;
    }

    // @brief Destroys an object on the pool, reuses the object memory
    void destroy(T* t)
    {
        t->~T();
        slot* s = reinterpret_cast<slot*>(t);
        s->next = free_list_;
        free_list_ = s;
        ++free_size_;
    }

    // @brief Calls desctructors of all allocated objects on the pool
    void destroy_all()
    {
        foreach ([this](T * t)
    {
        destroy(t);
        });
    }

    // @brief Visits all allocated objects on the pool
    template <typename Func>
    void foreach (const Func && func)
    {
        int freeNodes[N] = { 0 };
        auto next = free_list_;
        while (next)
        {
            auto i = next - buffer_;
            freeNodes[i] = 1;
            next = next->next;
        }

        for (int i = 0; i < N; ++i)
        {
            if (!freeNodes[i])
            {
                func(&buffer_[i].obj);
            }
        }
    }

    // @brief Checks if the object could possibly belong to the pool
    bool belongs(const T* t) const
    {
        return t >= &buffer_[0].obj && t <= &buffer_[N - 1].obj;
    }

    // @brief Checks whether the object was indeed allocated on the pool
    bool allocated(const T* t) const
    {
        assert(belongs(t));
        auto o = free_list_;
        while (o)
        {
            if (&o->obj == t)
            {
                return false;
            }
            o = o->next;
        }
        return true;
    }

    // @brief Checks if the pool is memory, meaning  no objects were created on the pool
    bool empty() const
    {
        return free_size_ == N;
    }

    // @brief Checks if all memory was used up
    bool full() const
    {
        return free_size_ == 0;
    }

private:
    // @brief Returns the number of free slots
    int free_list_size()
    {
        auto cur = free_list_;
        auto ret = 0;
        while (cur)
        {
            ++ret;
            cur = cur->next;
        }
        return ret;
    }

    // @frief An object slot
    union slot
    {
        slot() {}
        ~slot() {}

        slot* next;
        T obj;
    } buffer_[N];

    slot* free_list_;
    int free_size_ = N;
};

// @brief Unlimited object pool
template <typename T, int N = 256>
class object_pool
{
public:
    using slab_type = fixed_object_pool<T, N>;

    // @brief Creates new object on the pool
    template <typename ... Ts>
    T* create(Ts&&... ts)
    {
        for (auto i = slabs_.rbegin(); i != slabs_.rend(); ++i)
        {
            auto& slab = **i;
            T* ret = slab.create(std::forward<Ts>(ts)...);
            if (ret)
            {
                return ret;
            }
        }

        slabs_.emplace_back(new slab_type);
        return slabs_.back()->create(std::forward<Ts>(ts)...);
    }

    // @brief Desctroys object, reuses memory
    void destroy(T* t)
    {
        for (auto i = slabs_.begin(); i != slabs_.end(); ++i)
        {
            auto& slab = **i;
            if (slab.belongs(t))
            {
                slab.destroy(t);
                if (slab.empty())
                {
                    slabs_.erase(i);
                }
                break;
            }
        }
    }

    // @brief Checks if object was allocated on the pool
    bool allocated(const T* t) const
    {
        for (auto i = slabs_.begin(); i != slabs_.end(); ++i)
        {
            auto& slab = **i;
            if (slab.belongs(t))
            {
                return slab.allocated(t);
            }
        }
        throw;
    }

    // @brief Visits every allocated objects
    template <typename Func>
    void foreach (Func && func)
    {
        for (auto& slab : slabs_)
        {
            slab->foreach(std::forward<Func>(func));
        }
    }

    // @brief Desctoys all objects, clears memory
    void destroy_all()
    {
        slabs_.clear();
    }

private:
    std::vector<std::unique_ptr<slab_type>> slabs_;
};

} // namespace protoserv
