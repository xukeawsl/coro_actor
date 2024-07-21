#include "coro_actor_ctx.h"

asio::awaitable<void> svc_echo_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp);

extern "C" void svc_echo(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    asio::co_spawn(g_actor_ctx->io_contex(), svc_echo_impl(rqst, resp), asio::detached);
}

asio::awaitable<void> svc_echo_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    SPDLOG_INFO("call svc_echo_impl");
    resp->type = rqst->type;
    resp->value = std::move(rqst->value);
    g_actor_ctx->notify_complete();
    co_return;
}