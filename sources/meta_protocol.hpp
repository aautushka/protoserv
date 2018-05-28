#pragma once
#include "meta.hpp"
#include <stdint.h>

namespace meta
{

using int32=int32_t;

template <class... Message>
struct pack
{
};

template <int N, typename ... Messages>
struct proto_base;

template <int N>
struct proto_base<N>
{
    static constexpr int identify();
};

template <int N, typename Message, typename ... Ms>
struct proto_base<N, Message, Ms...> : proto_base < N + 1, Ms... >
{
    using proto_base < N + 1, Ms... >::identify;

    static constexpr int identify(tag<Message>)
    {
        return N;
    }
};

// @brief Returns message index in the parent protocol
template <typename Protocol, typename Message>
constexpr int identify()
{
    return Protocol::identify(tag<Message>());
}

// @brief Describes network protocol, each template type should be a protobuf
template <typename ... Ts>
using protocol = proto_base<0, Ts...>;

// @brief A shortcut for protocol
template <typename ... Ts>
using proto = protocol<Ts...>;

static_assert(0 == identify<protocol<int, char>, int>(), "");
static_assert(1 == identify<protocol<int, long>, long>(), ""); // NOLINT(runtime/int)
static_assert(2 == identify<proto_base<1, int, long>, long>(), ""); // NOLINT(runtime/int)

// @brief A sub-protocol class
// @description
// Sub-protocol is but a subset of a greater network protocol,
// it contains some messages and leaves the others out.
// Intended for faster compilation and lesser runtime-overhead.
template <typename Protocol, typename ... Messages>
struct subprotocol;

template <typename Protocol>
struct subprotocol<Protocol>
{
    static constexpr int identify();
};

template <typename Protocol, typename Message, typename ... Ms>
struct subprotocol<Protocol, Message, Ms...> : subprotocol<Protocol, Ms...>
{
    using subprotocol<Protocol, Ms...>::identify;

    static constexpr int identify(tag<Message>)
    {
        return Protocol::identify(tag<Message>());
    }
};

// @brief A shortcut for subprotocol
template <typename Protocol, typename ... Messages>
using subp = subprotocol<Protocol, Messages...>;

static_assert(0 == identify<subprotocol<protocol<int>, int>, int>(), "");
static_assert(1 == identify<subprotocol<protocol<long, int>, int>, int>(), ""); // NOLINT(runtime/int)
static_assert(2 == identify<subprotocol<protocol<long, char, int>, int>, int>(), ""); // NOLINT(runtime/int)
static_assert(0 == identify<subprotocol<protocol<long, int>, long>, long>(), ""); // NOLINT(runtime/int)

// @brief A protocol implemented in terms of subprotocol
// @description Every protocol message is found in subprotocol
template <typename ... Message>
using complete_protocol = subprotocol<protocol<Message...>, Message...>;
} // namespace meta

