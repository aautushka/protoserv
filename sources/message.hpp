#pragma once

namespace protoserv
{
/*
@brief A structure representing the protobuf message
*/
struct Message
{
    // message id
    int type;

    // probobuf message buffer, does not include the id/size header
    const void* data;

    // message buffer size, does not include the id/size header
    int size;
};

}//namespace protoserv
