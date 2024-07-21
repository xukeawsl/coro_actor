#pragma once

#include "coro_actor_common.h"
#include "coro_actor_pack.h"

struct coro_actor_msg_t {
    std::string service_name;
    std::string call_id;
    coro_actor_pack_t pack;
    std::shared_ptr<asio::ip::tcp::socket> socket;
    std::shared_ptr<asio::local::stream_protocol::socket> unix_socket;
};