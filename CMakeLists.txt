cmake_minimum_required(VERSION 3.5)

project(protoserv)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS system thread unit_test_framework)
include_directories(${Boost_INCLUDE_DIRS})

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

add_compile_options(-std=gnu++1z)

include_directories(sources)

add_subdirectory(sources)
add_subdirectory(tests)
