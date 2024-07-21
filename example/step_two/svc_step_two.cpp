#include "coro_actor_ctx.h"

asio::awaitable<void> svc_step_two_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp);

extern "C" void svc_step_two(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    asio::co_spawn(g_actor_ctx->io_contex(), svc_step_two_impl(rqst, resp), asio::detached);
}

asio::awaitable<void> svc_step_two_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {

    resp->type = rqst->type;

    SPDLOG_INFO("rqst value: {}", rqst->value);

    rqst->value += "\nstep two over !!!";

    co_await g_actor_ctx->send("svc_echo", rqst, resp);

    g_actor_ctx->notify_complete();
    co_return;
}