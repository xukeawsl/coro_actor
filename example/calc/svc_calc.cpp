#include "coro_actor_ctx.h"

asio::awaitable<void> svc_calc_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp);
asio::awaitable<void> svc_step_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp);

extern "C" void svc_calc(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    asio::co_spawn(g_actor_ctx->io_contex(), svc_calc_impl(rqst, resp), asio::detached);
}

extern "C" void svc_step(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    asio::co_spawn(g_actor_ctx->io_contex(), svc_step_impl(rqst, resp), asio::detached);
}

asio::awaitable<void> svc_calc_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    
    if (rqst->type == 1) {
        bool ok = co_await g_actor_ctx->send("svc_add", rqst, resp);
        if (!ok) {
            resp->value = g_actor_ctx->get_error_msg();
        }
    }

    g_actor_ctx->notify_complete();
    co_return;
}

asio::awaitable<void> svc_step_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {

    co_await g_actor_ctx->send("svc_step_one", rqst, resp);

    rqst->value = resp->value;

    SPDLOG_INFO("rqst value : {}", rqst->value);

    co_await g_actor_ctx->send("svc_step_two", rqst, resp);

    g_actor_ctx->notify_complete();
    co_return;
}