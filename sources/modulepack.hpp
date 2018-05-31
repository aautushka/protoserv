#pragma once
#include "server.hpp"
#include "meta_protocol.hpp"
#include "dispatch_table.hpp"
#include "components.hpp"
#include <string>
#include <iostream>

using application_server = protoserv::app_server;

// @brief A protobuf message types aware TCP/IP server
template <class Module, class ComponentPack, class ... Protocol>
class module_pack;

template <class Module, class ComponentPack, class Protocol, class ... Messages>
class module_pack<Module, ComponentPack, meta::subprotocol<Protocol, Messages...>> :
            public application_server, public ComponentPack
{
public:
    using ServerConnection = application_server::ServerConnection;
    using ClientConnection = application_server::ClientConnection;
    using protocol_pack = meta::subprotocol<Protocol, Messages...>;

    // @brief A wrapper around server connection
    // @description
    // Works with server connections only, dispaches server events
    // to the Handler passed as a template parameter. Allows
    // for different server of the same type handled by different objects
    template <typename Handler>
    class Client
    {
    public:
        explicit Client(Handler& handler)
            : handler_(&handler)
        {
        }

        Client()
            : handler_(nullptr)
        {
        }

        // @brief unsubscribes from server events
        ~Client()
        {
            conn_->onConnected = [](auto&) {};
            conn_->onDisconnected = [](auto&) {};
            conn_->onMessage = [](auto&, auto&) {};
        }

        // @brief Sets server connections handler
        void set_handler(Handler& handler)
        {
            assert(handler_ == nullptr);
            handler_ = &handler;
        }

        // @brief Sends protobuf message to the server
        template <typename Message>
        void send(Message& msg)
        {
            module_pack::send_message(*conn_, msg);
        }

        // @brief Sends protobuf message to the server
        // @description
        // It's not advised to use this method at all, it allow user
        // to bypass the message checking mechanism
        void send(int messageid, google::protobuf::Message& message)
        {
            conn_->send(messageid, message);
        }

        // @brief Passes the server connection event to the handler
        void handle_connected(ServerConnection& conn)
        {
            assert(conn_ == nullptr || &conn == conn_);
            meta::call_on_connected(*handler_, conn, 0);
            conn_ = &conn;
        }

        // @brief Passes the server disconnect event to the handler
        void handle_disconnected(ServerConnection& conn)
        {
            assert(&conn == conn_);
            meta::call_on_disconnected(*handler_, conn, 0);
        }

        // @brief Passes the received server message to the handler
        void handle_message(ServerConnection& conn, const protoserv::Message& msg)
        {
            assert(&conn == conn_);

            auto id = msg.type;
            auto buf = msg.data;
            auto len = msg.size;

            meta::dispatcher<Module, protocol_pack, Messages...>::dispatch(
                *handler_, *conn_, id, buf, len);
        }

        // @brief Assignes server connection
        void Connect(ServerConnection& conn)
        {
            assert(conn_ == nullptr || &conn == conn_);
            conn_ = &conn;
        }

        Client(const Client&) = delete;
        Client& operator =(const Client&) = delete;

    private:
        Handler* handler_ = nullptr;;
        ServerConnection* conn_ = nullptr;
    };

    // @brief A server connection handler which construct handler in place
    template <typename Handler>
    class ClientOwner : public Handler, public Client<Handler>
    {
    public:
        ClientOwner()
        {
            Client<Handler>::set_handler(*this);
        }

        ~ClientOwner() {}
    };

    // @brief Asynchrounous server connection handler
    class AsyncHandler
    {
    public:
        void onConnected(ServerConnection&)
        {
            connected_ = true;
        }

        void onDisconnected(ServerConnection&)
        {
            connected_ = false;
            dispatcher_.cancel();
        }

        template <typename Message>
        void onMessage(ServerConnection&, Message& msg)
        {
            dispatcher_.dispatch(msg);
        }

        template <typename MessageHandler>
        void receive(MessageHandler&& handler)
        {
            dispatcher_.subscribe(std::forward<MessageHandler>(handler));
            if (!connected_)
            {
                dispatcher_.cancel();
            }
        }

    private:
        protoserv::dispatch_table<Messages...> dispatcher_;
        bool connected_ = false;
    };

    // @brief Sends protobuf message to the given client/server
    template <typename Connection, typename T>
    static void send_message(Connection& conn, T&& message)
    {
        using TT = std::decay_t<T>;
        auto messageId = meta::identify<Protocol, TT>();
        conn.send(messageId, message);
    }

    // @brief Sends protobuf message to the give client/server
    template <typename Connection, typename T>
    static void send_message(Connection* conn, T&& message)
    {
        send_message(*conn, message);
    }

    // @brief Create synchronous server connection handler
    template <typename Handler>
    auto handle_server(Handler& handler, const std::string& ip, uint16_t port)
    {
        auto client = std::make_shared<Client<Handler>>(handler);
        handle_server(*client, ip, port);
        return client;
    }

    // @brief Creates ynchronous server connection handler
    template <typename Handler>
    auto handle_server_sync(Handler& handler, const std::string& ip, uint16_t port)
    {
        auto client = std::make_shared<Client<Handler>>(handler);
        auto ptr = client.get();
        auto& conn = connect_to_server(ip, port,
                                       [ptr](auto & conn, auto && message)
        {
            ptr->handle_message(conn, std::forward<decltype(message)>(message));
        },
        [ptr](auto & conn)
        {
            ptr->handle_connected(conn);
        },
        [ptr](auto & conn)
        {
            ptr->handle_disconnected(conn);
        });

        client->Connect(conn);
        return client;
    }

    // @brief Create asynchronous server connection haandler
    template <typename Handler>
    void handle_server(Client<Handler>& client, const std::string& ip, uint16_t port)
    {
        auto ptr = &client;

        auto& conn = async_connect(ip, port,
                                   [ptr](auto & conn, auto & message)
        {
            ptr->handle_message(conn, message);
        },
        [ptr](auto & conn)
        {
            ptr->handle_connected(conn);
        },
        [ptr](auto & conn)
        {
            ptr->handle_disconnected(conn);
        });

        client.Connect(conn);
    }

    // @brief Creates asynchronous server connection handler
    template <typename Handler>
    auto handle_server(const std::string& ip, uint16_t port)
    {
        auto owner = std::make_shared<ClientOwner<Handler>>();
        Client<Handler>& client = *owner;

        handle_server(client, ip, port);
        return owner;
    }

    // @brief Creates asynchronous server connection handler
    auto handle_server_async(const std::string& ip, uint16_t port)
    {
        return handle_server<AsyncHandler>(ip, port);
    }

    // @brief Create asynchronous server connection handler
    auto handle_server_async(ServerConnection& conn)
    {
        auto owner = std::make_shared<ClientOwner<AsyncHandler>>();
        Client<AsyncHandler>& client = *owner;
        client.Connect(conn);
        owner->onConnected(conn);
        conn.onConnected = [&client](auto & c)
        {
            client.handle_connected(c);
        };
        conn.onDisconnected = [&client](auto & c)
        {
            client.handle_disconnected(c);
        };
        conn.onMessage = [&client](auto & c, auto & m)
        {
            client.handle_message(c, m);
        };
        return owner;
    }

    // @brief Construct server and subscribes to server events
    module_pack()
    {
        subscribe_client([this](auto & conn, auto msg)
        {
            dispatch_client(conn, msg);
        });
        subscribe_server([this](auto & conn, auto msg)
        {
            dispatch_server(conn, msg);
        });

        onClientConnected = [this](auto & session)
        {
            dispatch_connected(session);
        };
        onClientDisconnected = [this](auto & session)
        {
            dispatch_disconnected(session);
        };

        onServerConnected = [this](auto & session)
        {
            dispatch_connected(session);
        };
        onServerDisconnected = [this](auto & session)
        {
            dispatch_disconnected(session);
        };

        onCommandReceived = [this](auto cmd)
        {
            dispatch_command(cmd);
        };

        onApplicationInitialized = [this]()
        {
            dispatch_initialized();
        };
        onApplicationDeinitialized = [this]()
        {
            dispatch_deinitialized();
        };

        onConfigurationLoaded = [this](auto && conf)
        {
            dispatch_configuration(conf);
        };
    }

private:
    // @brief Dispatches client message to the derived class and its components
    void dispatch_client(ClientConnection& conn, const protoserv::Message& msg)
    {
        dispatch_message(conn, msg.type, msg.data, msg.size);

        ComponentPack::template dispatch_component_message<protocol_pack, Messages...>(
            conn, msg.type, msg.data, msg.size);
    }

    // @brief Dispatches server message to the derived class, server handler and components
    void dispatch_server(ServerConnection& conn, const protoserv::Message& msg)
    {
        dispatch_message(conn, msg.type, msg.data, msg.size);

        ComponentPack::template dispatch_component_message<protocol_pack, Messages...>(
            conn, msg.type, msg.data, msg.size);
    }


    // @brief Dispatches client/server message to the derived class and components
    template <class Connection>
    void dispatch_message(Connection& conn, int id, const void* buf, int len)
    {
        auto& mod = static_cast<Module&>(*this);
        meta::dispatcher<Module, Protocol, Messages...>::dispatch(mod, conn, id, buf, len);
    }

    // @brief Dispatches connection event to the derived class and components
    template <typename Connection>
    void dispatch_connected(Connection& session)
    {
        auto& mod = static_cast<Module&>(*this);
        meta::call_on_connected(mod, session, 0);
        ComponentPack::open_component_connection(session);
    }

    // @brief Dispaches diconnected event to the derived class and components
    template <typename Connection>
    void dispatch_disconnected(Connection& session)
    {
        auto& mod = static_cast<Module&>(*this);
        meta::call_on_disconnected(mod, session, 0);
        ComponentPack::close_component_connection(session);
    }

    // @brief Dispatches stdin command to the derived class
    void dispatch_command(const protoserv::command& cmd)
    {
        auto& mod = static_cast<Module&>(*this);
        meta::call_on_command(mod, cmd, 0);
    }

    // @brief Dispatches application initialization event to the derived class and components
    void dispatch_initialized()
    {
        auto& mod = static_cast<Module&>(*this);
        meta::call_on_initialized(mod, 0);
        ComponentPack::initialize_component();
    }

    // @brief Dispatches application deinitialization event to the derived class and components
    void dispatch_deinitialized()
    {
        auto& mod = static_cast<Module&>(*this);
        meta::call_on_deinitialized(mod, 0);
        ComponentPack::deinitialize_component();
    }

    // @brief Dispaches configuration change event to the derived class and components
    void dispatch_configuration(const protoserv::Options& conf)
    {
        auto& mod = static_cast<Module&>(*this);
        meta::call_on_configuration(mod, conf, 0);
        ComponentPack::configure_component(conf);
    }
};
