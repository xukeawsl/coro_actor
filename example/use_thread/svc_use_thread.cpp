#include "coro_actor_ctx.h"

asio::awaitable<void> svc_use_thread_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp);

extern "C" void svc_use_thread(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    asio::co_spawn(g_actor_ctx->io_contex(), svc_use_thread_impl(rqst, resp), asio::detached);
}

asio::awaitable<void> svc_use_thread_impl(coro_actor_pack_t* rqst, coro_actor_pack_t* resp) {
    /* 利用 shared_ptr 的 use_count 来进行通知 */
    auto complete_nofity = std::make_shared<asio::steady_timer>(g_actor_ctx->io_contex());

    complete_nofity->expires_at(std::chrono::steady_clock::time_point::max());

    asio::error_code ec;
    std::atomic<int> cnt = 0;

    for (int i = 0; i < 3; i++) {
        std::thread([complete_nofity, &cnt] {
            for (int j = 0; j < 1000000; j++) {
                cnt++;
            }

            /* 最后一个线程退出时负责唤醒等待中的协程 */
            if (complete_nofity.use_count() == 2) {
                SPDLOG_INFO("weak up corotinue");
                complete_nofity->cancel_one();
            }
        }).detach();
    }

    co_await complete_nofity->async_wait(asio::redirect_error(asio::use_awaitable, ec));

    SPDLOG_INFO("result: {}", cnt.load());

    g_actor_ctx->notify_complete();
    co_return;
}