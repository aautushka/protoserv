#pragma once
#include "async_pack.hpp"

namespace protoserv
{
template <typename Protocol, typename ... Messages>
struct protocol_operations
{
    template <typename T>
    struct get_message_id
    {
        using TT = std::remove_cv_t<T>;
        static constexpr int value = meta::identify<Protocol, TT>();
    };

    using dispatcher = table_dispatcher<Protocol, Messages...>;
    using table = dispatch_table<Protocol, Messages...>;
};

template <class ... Protocol>
class async_client
    : public async_client_pack <
      protocol_operations,
      meta::complete_protocol<Protocol...>
      > {};

template <class ... Protocol>
class async_client<meta::proto<Protocol...>>
            : public async_client_pack <
              protocol_operations,
              meta::complete_protocol<Protocol...>
              > {};

template <class Protocol, class ... Messages>
class async_client<meta::subprotocol<Protocol, Messages...>>
            : public async_client_pack <
              protocol_operations,
              meta::subprotocol<Protocol, Messages... >> {};

} // namespace protoserv
