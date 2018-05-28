cmake_minimum_required(VERSION 3.5)

project(protoserv)

find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

add_compile_options(-std=gnu++1z)


add_subdirectory(sources)
