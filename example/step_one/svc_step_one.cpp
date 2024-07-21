#include "coro_actor_ctx.h"

asio::awaitable<void> svc_step_one_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp);

extern "C" void svc_step_one(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    asio::co_spawn(g_actor_ctx->io_contex(), svc_step_one_impl(rqst, resp), asio::detached);
}

asio::awaitable<void> svc_step_one_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {

    resp->type = rqst->type;

    SPDLOG_INFO("rqst value: {}", rqst->value);

    resp->value = rqst->value + "step one over !!!";

    g_actor_ctx->notify_complete();
    co_return;
}