#pragma once

#include "coro_actor_common.h"

#include "coro_actor_pack.h"
#include "coro_actor_msg.h"
#include "coro_actor_protocol.h"

class coro_actor_ctx {
public:
    using func_t = void (*)(coro_actor_pack_t*, coro_actor_pack_t*);

public:
    coro_actor_ctx(asio::io_context& io_ctx, std::shared_ptr<asio::local::stream_protocol::socket> unix_socket,
                   std::unordered_map<std::string, coro_actor_ctx::func_t> service_map);

    asio::io_context& io_contex();

    bool contains(const std::string& service_name);

    asio::awaitable<bool> send(const std::string& service_name, const coro_actor_pack_t* request_pack, coro_actor_pack_t* response_pack);

    void set_call_id(const std::string& call_id);

    std::string get_error_msg() const;

    void push_msg(coro_actor_msg_t&& msg);

    void notify_complete();

    void set_response_pack(coro_actor_response_t&& resp_pack);

private:
    asio::awaitable<void> handle_message();

    void set_error_msg(const std::string& msg);

private:
    asio::io_context& io_ctx_;
    std::shared_ptr<asio::local::stream_protocol::socket> unix_socket_;

    pid_t pid_;
    uint32_t seq_;
    std::string error_msg_;

    /* 调用 id, 格式为 pid-seq */
    std::string call_id_;

    /* 存放服务名与其对应的处理函数指针 */
    std::unordered_map<std::string, func_t> service_map_;

    /* 消息接收队列, 缓存远程客户端发来的请求 */
    std::queue<coro_actor_msg_t> msg_queue_;
    asio::steady_timer msg_notify_timer_;

    /* 服务调用通知器 */
    asio::steady_timer service_notify_timer_;

    /* 响应通知器 */
    asio::steady_timer response_notify_tiemr_;

    /* 当前调用对应的响应 */
    coro_actor_response_t curr_resp_pack_;

    /* 等待标志 */
    bool is_waiting;
};

extern coro_actor_ctx* g_actor_ctx;