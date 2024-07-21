#pragma once

#include "coro_actor_common.h"

#include "coro_actor_pack.h"

/* 定义内部通用的结构 */

#define CORO_ACTOR_CONNECT      0x01
#define CORO_ACTOR_CONNACK      0x02
#define CORO_ACTOR_REQUEST      0x03
#define CORO_ACTOR_RESPONSE     0x04
#define CORO_ACTOR_NOT_SERVICE  0x05

struct coro_actor_connect_t {
    pid_t pid;                   /* Actor 进程 ID */
    std::string name;            /* Actor 节点名称 */
};

struct coro_actor_connack_t {
    bool result;                 /* Proxy 进程处理结果 */
};

struct coro_actor_request_t {
    pid_t pid;                         /* 进程 ID */
    std::string       call_id;         /* 调用 ID */
    std::string       service_name;    /* 服务名 */
    coro_actor_pack_t request_pack;    /* 请求数据 */
};

struct coro_actor_response_t {
    pid_t pid;                         /* 进程 ID */
    std::string call_id;               /* 调用 ID */
    std::string service_name;          /* 服务名 */
    coro_actor_pack_t response_pack;   /* 响应数据 */
};