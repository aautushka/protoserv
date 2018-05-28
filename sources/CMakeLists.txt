set(SRC 
    async_client.hpp
    async_pack.hpp
    async_stdin.hpp
    basic_session.hpp
    client_session.hpp
    CMakeLists.txt
    components.hpp
    dispatch_table.hpp
    messagebuf.hpp
    message.hpp
    meta.hpp
    meta_protocol.hpp
    module.hpp
    modulepack.hpp
    object_pool.hpp
    server.cpp
    server.hpp
    server_session.hpp
    session_buffers.hpp
    timer.hpp
)

add_library(protoserv ${SRC})
