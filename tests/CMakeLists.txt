set(SRC 
    main.cpp
    basic_session_test
    async_client_test
)

add_library(protobuf_messages protobuf_messages/messages.pb.cc)
add_executable(tests ${SRC})

target_link_libraries(tests protoserv protobuf_messages ${Boost_LIBRARIES} ${PROTOBUF_LIBRARY})
