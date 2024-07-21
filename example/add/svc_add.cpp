#include "coro_actor_ctx.h"

asio::awaitable<void> svc_add_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp);

extern "C" void svc_add(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    asio::co_spawn(g_actor_ctx->io_contex(), svc_add_impl(rqst, resp), asio::detached);
}

asio::awaitable<void> svc_add_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    std::istringstream in(rqst->value);

    int a, b, result;

    in >> a >> b;

    result = a + b;

    SPDLOG_INFO("{} + {} = {}", a, b, result);

    resp->type = rqst->type;
    resp->value = std::to_string(result);

    g_actor_ctx->notify_complete();
    co_return;
}