#pragma once
#include "server.hpp"
#include "boost/format.hpp"
#include "dispatch_table.hpp"
#include <string>

namespace meta
{
//
// reply_message
//
template <typename Module, typename Reply>
struct reply_message
{
    template <typename F, typename Conn>
    static void call(F&& f, Conn& c)
    {
        Module::send_message(c, f());
    }
};

template <typename Module>
struct reply_message<Module, void>
{
    template <typename F, typename Conn>
    static void call(F&& f, Conn& c)
    {
        f();
    }
};

inline void throw_with_buffer(const void* buf, int len)
{
    //FIXME:temporarily logging
    boost::format msg("Unknown message format received. size = %1% buffer = %2%");
    auto* ptr = static_cast<const char*>(buf);
    std::string param;
    for (auto i = 0; i < len; ++i)
    {
        param += std::to_string(static_cast<int>(ptr[i]));
        param += " ";
    }
    msg % len % param;
    throw std::runtime_error(msg.str());
}

//
// call_on_message_alt_2
//

// @brief Calls component's onMessage(Message&) if present
template <typename Msg, typename Module, typename Comp, typename Connection>
auto call_on_message_alt_2(Comp& comp, Connection& conn, const void* buf, int len, int)
-> decltype(comp.onMessage(std::declval<Msg&>()), void())
{
    auto func = [&comp, buf, len]() -> decltype(auto)
    {
        Msg message;
        if (!message.ParseFromArray(buf, len))
        {
            throw_with_buffer(buf, len);
        }
        return comp.onMessage(message);
    };

    reply_message<Module, decltype(func())>::call(func, conn);
}

template <typename Msg, typename Module, typename Comp, typename Connection>
void call_on_message_alt_2(Comp&, Connection&, const void*, int, long) // NOLINT(runtime/int)
{
    // do nothing, ignore the message
    // since the class does not define onMessage(conn, msg)
}

//
// call_on_message_alt
//

// @brief Calls component's onMessage(Connection*, Message&) if present or check the alternatives
template <typename Msg, typename Module, typename Comp, typename Connection>
auto call_on_message_alt(Comp& comp, Connection& conn, const void* buf, int len, int)
-> decltype(comp.onMessage(&conn, std::declval<Msg&>()), void())
{
    auto func = [&comp, &conn, buf, len]() -> decltype(auto)
    {
        Msg message;
        if (!message.ParseFromArray(buf, len))
        {
            throw_with_buffer(buf, len);
        }
        return comp.onMessage(&conn, message);
    };

    reply_message<Module, decltype(func())>::call(func, conn);
}

template <typename Msg, typename Module, typename Comp, typename Connection>
auto call_on_message_alt(Comp& comp, Connection& conn, const void* buf, int len, long) // NOLINT(runtime/int)
{
    // try another alternative, taking no connection in the arg list
    return call_on_message_alt_2<Msg, Module>(comp, conn, buf, len, 0);
}

//
// call_on_message
//

// @brief Calls camponent's onMessage(Connection&, Messsage&) if present, check the alternatives
// @description
// The order of precedence:
// 1. onMessage(Connection&, Message&)
// 2. onMessage(Connection&, Message*)
// 3. onMessage(Message&)
template <typename Msg, typename Module, typename Comp, typename Connection>
auto call_on_message(Comp& comp, Connection& conn, const void* buf, int len, int)
-> decltype(comp.onMessage(conn, std::declval<Msg&>()), void())
{
    auto func = [&comp, &conn, buf, len]() -> decltype(auto)
    {
        Msg message;
        if (!message.ParseFromArray(buf, len))
        {
            throw_with_buffer(buf, len);
        }
        return comp.onMessage(conn, message);
    };

    reply_message<Module, decltype(func())>::call(func, conn);
}

template <typename Msg, typename Module, typename Comp, typename Connection>
auto call_on_message(Comp& comp, Connection& conn, const void* buf, int len, long) // NOLINT(runtime/int)
{
    // try the pointer alternative
    return call_on_message_alt<Msg, Module>(comp, conn, buf, len, 0);
}

//
// call_on_connected_alt
//

// @brief Calls component's onConnected(Connection*)
template <typename Module, typename Connection>
auto call_on_connected_alt(Module& mod, Connection& conn, int) ->
decltype(mod.onConnected(&conn))
{
    mod.Module::onConnected(&conn);
}

template <typename Module, typename Connection>
auto call_on_connected_alt(Module&, Connection&, long) // NOLINT(runtime/int)
{
    // do nothing, ignore the event
    // the class does not define onConnected(conn)
}

//
// call_on_connected
//

// @brief Calls component's onConnected(Connection&)
template <typename Module, typename Connection>
auto call_on_connected(Module& mod, Connection& conn, int) ->
decltype(mod.onConnected(conn))
{
    mod.Module::onConnected(conn);
}

template <typename Module, typename Connection>
auto call_on_connected(Module& mod, Connection& conn, long) // NOLINT(runtime/int)
{
    // try the pointer alternative
    call_on_connected_alt(mod, conn, 0);
}

//
// call_on_disconnected_alt
//

// @brief Calls component's onDisconnected(Connection*)
template <typename Module, typename Connection>
auto call_on_disconnected_alt(Module& mod, Connection& conn, int) ->
decltype(mod.onDisconnected(&conn))
{
    mod.Module::onDisconnected(&conn);
}

template <typename Module, typename Connection>
auto call_on_disconnected_alt(Module&, Connection&, long) // NOLINT(runtime/int)
{
    // do nothing, ignore the event
    // the class does not define onDisconnected(conn)
}

//
// call_on_disconnected
//

// @brief Calls component's onDisconnected(Connection&) or check the alternatives
template <typename Module, typename Connection>
auto call_on_disconnected(Module& mod, Connection& conn, int) ->
decltype(mod.onDisconnected(conn))
{
    mod.Module::onDisconnected(conn);
}

template <typename Module, typename Connection>
auto call_on_disconnected(Module& mod, Connection& conn, long) // NOLINT(runtime/int)
{
    // try the pointer alternative
    call_on_disconnected_alt(mod, conn, 0);
}

//
// call_on_command
//

// @brief Calls module's onCommand(command&)
template <typename Module, typename Command>
auto call_on_command(Module& mod, const Command& cmd, int) ->
decltype(mod.onCommand(cmd))
{
    mod.Module::onCommand(cmd);
}

template <typename Module, typename Command>
auto call_on_command(Module&, const Command& cmd, long) // NOLINT(runtime/int)
{
    // do nothing, ignore the event
    // the class does not define onCommand(conn)
}

//
// call_on_initialized
//

// @brief calls module's onInitialized() if present
template <typename Module>
auto call_on_initialized(Module& mod, int) ->
decltype(mod.onInitialized())
{
    mod.onInitialized();
}

template <typename Module>
void call_on_initialized(Module& mod, long) // NOLINT(runtime/int)
{
}

//
// call_on_deinitialized
//

// @brief Calls module's onDeinitialized() if present
template <typename Module>
auto call_on_deinitialized(Module& mod, int) ->
decltype(mod.onDeinitialized())
{
    mod.onDeinitialized();
}

template <typename Module>
void call_on_deinitialized(Module& mod, long) // NOLINT(runtime/int)
{
}

//
// call_on_configuration
//

// @brief Calls module's onConfiguratoin(Configuration&) if presnet
template <typename Module, typename Configuration>
auto call_on_configuration(Module& mod, Configuration&& conf, int) ->
decltype(mod.onConfiguration(std::forward<Configuration>(conf)))
{
    mod.onConfiguration(std::forward<Configuration>(conf));
}

template <typename Module, typename Configuration>
void call_on_configuration(Module& mod, Configuration&& conf, long) // NOLINT(runtime/int)
{
}

//
// dispatcher
//

// @brief Protocol message dispatcher
// @description
// Translates the integer id to message type and tries to call the message handler if present.
// If message handler is not present, then ignores the message
template <class Module, class... Ts>
struct dispatcher;

template<class Module, class Protocol>
struct dispatcher<Module, Protocol>
{
    template <typename Mod, typename Connection>
    static void dispatch(Mod& mod, Connection& conn, int id, const void* buf, int len)
    {
        //TODO: add logging here later
        //std::cout << "Unregistered id = " << id << " on module level" << std::endl;
        // Not found message handler on module level
        // Just ignore this node.
    }
};

template <class Module, class Protocol, class T, class... Ts>
struct dispatcher<Module, Protocol, T, Ts...>
{
    template <typename Component, typename Connection>
    static auto dispatch(Component& comp, Connection& conn, int id, const void* buf, int len)
    {
        if (id == meta::identify<Protocol, T>())
        {
            // message id matches the type index
            return call_on_message<T, Module>(comp, conn, buf, len, 0);
        }
        else
        {
            // look further
            return dispatcher<Module, Protocol, Ts...>::dispatch(comp, conn, id, buf, len);
        }
    }
};

//
// dispatch_component_message
//

// @brief Forward the message to all components
template <typename Dispatcher, typename Aggregate, typename Connection>
void dispatch_component_message(Aggregate&, Connection&, int id, const void* buf, int len)
{
    //TODO: add logging here later
    //std::cout << "Unregistered id = " << id << " on component level" << std::endl;
    // Not found message handler on component level
    // Just ignore this node.
}

template <typename Dispatcher, typename Aggregate, typename Connection, class Component, class ... Cs>
void dispatch_component_message(Aggregate& agg, Connection& conn, int id, const void* buf, int len)
{
    Component& comp = static_cast<Component&>(agg);
    Dispatcher::dispatch(comp, conn, id, buf, len);

    dispatch_component_message<Dispatcher, Aggregate, Connection, Cs...>(agg, conn, id, buf, len);
}

//
// open_component_connection
//

// @brief Notifies all components about new connection
template <typename Aggregate, typename Connection>
void open_component_connection(Aggregate&, Connection&)
{
}

template <typename Aggregate, typename Connection, class Component, class ... Cs>
void open_component_connection(Aggregate& agg, Connection& conn)
{
    Component& comp = static_cast<Component&>(agg);
    call_on_connected(comp, conn, 0);
    open_component_connection<Aggregate, Connection, Cs...>(agg, conn);
}

//
// close_component_connection
//

// @brief Notifies all components about closed connection
template <typename Aggregate, typename Connection>
void close_component_connection(Aggregate&, Connection&)
{
}

template <typename Aggregate, typename Connection, class Component, class ... Cs>
void close_component_connection(Aggregate& agg, Connection& conn)
{
    Component& comp = static_cast<Component&>(agg);
    call_on_disconnected(comp, conn, 0);
    close_component_connection<Aggregate, Connection, Cs...>(agg, conn);
}

//
// initialize_component
//

// @brief Initializes components
template <typename Aggregate> void initialize_component(Aggregate&) {}

template <typename Aggregate, class Component, class ... Cs>
void initialize_component(Aggregate& agg)
{
    auto& comp = static_cast<Component&>(agg);
    call_on_initialized(comp, 0);
    initialize_component<Aggregate, Cs...>(agg);
}

//
// deinitialize_component
//

// @brief Deinitializes components
template <typename Aggregate> void deinitialize_component(Aggregate&) {}

template <typename Aggregate, class Component, class ... Cs>
void deinitialize_component(Aggregate& agg)
{
    auto& comp = static_cast<Component&>(agg);
    call_on_deinitialized(comp, 0);
    deinitialize_component<Aggregate, Cs...>(agg);
}

//
// configure_component
//

// @brief Configures components
template <typename Aggregate, typename Configuration>
void configure_component(Aggregate&, Configuration&&) {}

template <typename Aggregate, typename Configuration, class Component, class ... Cs>
void configure_component(Aggregate& agg, Configuration&& conf)
{
    auto& comp = static_cast<Component&>(agg);
    call_on_configuration(comp, std::forward<Configuration>(conf), 0);
    configure_component<Aggregate, Configuration, Cs...>(agg, std::forward<Configuration>(conf));
}

//
// post_component
//

// @brief Sends message of arbitrary type to component.
// @description
// Component should supply a onMesasge(T) overload in order to receive the message.
// Everything works statically, no performance overhead - it's just a function call.
template <typename Aggregate, typename Message>
void post_component(Aggregate&, Message&&)
{
}

template <typename Aggregate, typename Message, class Component, class ... Cs>
auto post_component(Aggregate& agg, Message&& msg, int)
-> decltype(static_cast<Component&>(agg).onMessage(std::forward<Message>(msg)))
{
    return static_cast<Component&>(agg).onMessage(std::forward<Message>(msg));
}

template <typename Aggregate, typename Message, class Component, class ... Cs>
auto post_component(Aggregate& agg, Message&& msg, long) // NOLINT(runtime/int)
{
    return post_component<Aggregate, Message, Cs...>(agg, std::forward<Message>(msg), 0);
}

} // namespace meta


// @brief Constructs and holds all components within the class instance
template <class Module, template <class> class ... Component>
struct component_aggregate : public Component<Module>...
{
    using self_type = component_aggregate;

    // @brief Stores reference to parent module
    explicit component_aggregate(Module& module)
        : module(module)
    {
    }

    // @brief Forwards message to all components
    template <typename Protocol, typename ... Messages, typename Connection>
    void dispatch_component_message(Connection& conn, int id, const void* buf, int len)
    {
        using Dispatcher = meta::dispatcher<Module, Protocol, Messages...>;
        meta::dispatch_component_message<Dispatcher, component_aggregate, Connection, Component<Module>...>
        (*this, conn, id, buf, len);
    }

    // @brief Notifies all components about new connection
    template <typename Connection>
    void open_component_connection(Connection& conn)
    {
        meta::open_component_connection
        <component_aggregate, Connection, Component<Module>...>
        (*this, conn);
    }

    // @brief Notifies all components about closed connection
    template <typename Connection>
    void close_component_connection(Connection& conn)
    {
        meta::close_component_connection
        <component_aggregate, Connection, Component<Module>...>
        (*this, conn);
    }

    // @brief Initializes all components
    void initialize_component()
    {
        meta::initialize_component<component_aggregate, Component<Module>...>(*this);
    }

    // @brief Deinitializes all components
    void deinitialize_component()
    {
        meta::deinitialize_component<self_type, Component<Module>...>(*this);
    }

    // @brief Configures all components
    template <typename Configuration>
    void configure_component(Configuration&& conf)
    {
        meta::configure_component<self_type, Configuration, Component<Module>...>(*this, std::forward<Configuration>(conf));
    }

    // @brief Sends a message of arbitrary type to component, indended for component-to-component communication
    template <typename Message>
    auto post_message(Message&& message)
    {
        return meta::post_component
               <self_type, Message, Component<Module>...>
               (*this, std::forward<Message>(message), 0);
    }

    Module& module;
};

// @brief A convenience wrapper arond component_aggregate, follows its interface as well
template <class Module, template <class> class ... Component>
class cpack
{
public:
    using aggregate_type = component_aggregate<Module, Component...>;

    cpack()
        : aggregate_(static_cast<Module&>(*this))
    {
    }

    template <typename Protocol, typename ... Messages, typename Connection>
    void dispatch_component_message(Connection& conn, int id, const void* buf, int len)
    {
        aggregate_.template dispatch_component_message<Protocol, Messages...>(conn, id, buf, len);
    }

    template <typename Connection>
    void open_component_connection(Connection& conn)
    {
        aggregate_.open_component_connection(conn);
    }

    template <typename Connection>
    void close_component_connection(Connection& conn)
    {
        aggregate_.close_component_connection(conn);
    }

    void initialize_component()
    {
        aggregate_.initialize_component();
    }

    void deinitialize_component()
    {
        aggregate_.deinitialize_component();
    }

    template <typename Configuration>
    void configure_component(Configuration&& conf)
    {
        aggregate_.configure_component(std::forward<Configuration>(conf));
    }

    template <template <class> class Comp>
    Comp<Module>& query_component()
    {
        return static_cast<Comp<Module>&>(aggregate_);
    }

    template <template <class> class Callee, template <class> class Caller>
    static Callee<Module>& call_component(Caller<Module>& caller)
    {
        aggregate_type& agg = static_cast<aggregate_type&>(caller);
        return static_cast<Callee<Module>&>(agg);
    }

    template <template <class> class Callee, template <class> class Caller>
    static Callee<Module>& call_component(Caller<Module>* caller)
    {
        aggregate_type& agg = static_cast<aggregate_type&>(*caller);
        return static_cast<Callee<Module>&>(agg);
    }

    template <template <class> class Caller>
    static Module& call_module(Caller<Module>& caller)
    {
        aggregate_type& agg = static_cast<aggregate_type&>(caller);
        return agg.module;
    }

    template <template <class> class Caller>
    static Module& call_module(Caller<Module>* caller)
    {
        aggregate_type& agg = static_cast<aggregate_type&>(*caller);
        return agg.module;
    }

    template <typename T, template <class> class Caller>
    static auto post_component(Caller<Module>& caller, T&& message)
    {
        aggregate_type& agg = static_cast<aggregate_type&>(caller);
        return agg.post_message(std::forward<T>(message));
    }

    template <typename T, template <class> class Caller>
    static auto post_component(Caller<Module>* caller, T&& message)
    {
        aggregate_type& agg = static_cast<aggregate_type&>(*caller);
        return agg.post_message(std::forward<T>(message));
    }

private:
    aggregate_type aggregate_;
};
