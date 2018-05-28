set(SRC 
    main.cpp
    basic_session_test
    async_client_test
    async_handler_test
    server_test
    server_events_test
    component_test
    module_test
    module_timer_test
    module_chart
    module_bench
    async_stdin_test
)

add_library(protobuf_messages protobuf_messages/messages.pb.cc)
add_executable(tests ${SRC})

target_link_libraries(tests protoserv protobuf_messages ${Boost_LIBRARIES} ${PROTOBUF_LIBRARY})
