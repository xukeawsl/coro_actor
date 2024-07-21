#pragma once

#include "coro_actor_common.h"

/* 解析用户请求, 包括调用的服务名和报文内容, 其中报文内容通过 */
/* 服务名配置的报文类型调用响应报文的解包函数, 解包完成后直接 */
/* 通过预加载的函数指针调用用户编写的业务处理函数(注意是同步调用的) */
/* 虽然是同步调用, 但是用户可以在业务处理函数中使用协程进行 io 操作 */

class coro_actor_session : public std::enable_shared_from_this<coro_actor_session>
{
public:
    coro_actor_session(asio::ip::tcp::socket socket);

    ~coro_actor_session();

    void start();

private:
    void stop();

    asio::awaitable<void> handle_remote_pack();

private:
    std::shared_ptr<asio::ip::tcp::socket> socket_;
};