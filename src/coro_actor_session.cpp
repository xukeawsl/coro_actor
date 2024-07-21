#include "coro_actor_session.h"
#include "coro_actor_ctx.h"

extern coro_actor_ctx* g_actor_ctx;

coro_actor_session::coro_actor_session(asio::ip::tcp::socket socket) 
    : socket_(std::make_shared<asio::ip::tcp::socket>(std::move(socket)))
{}

coro_actor_session::~coro_actor_session() {}

void coro_actor_session::stop() {
    asio::error_code ignore_ec;
    this->socket_->close(ignore_ec);
}

void coro_actor_session::start() {
    SPDLOG_INFO("new connect");
    asio::co_spawn(this->socket_->get_executor(), [self = shared_from_this()] {
        return self->handle_remote_pack();
    }, asio::detached);
}

asio::awaitable<void> coro_actor_session::handle_remote_pack() {
    asio::error_code ec;
    uint16_t service_length;
    std::string service_name;
    uint8_t type;
    uint32_t data_length;
    std::string request_data;

    while (this->socket_->is_open()) {

        co_await asio::async_read(*(this->socket_), asio::buffer(&service_length, 2), asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            SPDLOG_WARN("{}", ec.value());
            this->stop();
            co_return;
        }

        service_length = asio::detail::socket_ops::network_to_host_short(service_length);
        service_name.resize(service_length);

        co_await asio::async_read(*(this->socket_), asio::buffer(service_name.data(), service_name.length()), asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            SPDLOG_WARN("{}", ec.value());
            this->stop();
            co_return;
        }

        co_await asio::async_read(*(this->socket_), asio::buffer(&type, 1), asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            SPDLOG_WARN("{}", ec.value());
            this->stop();
            co_return;
        }

        co_await asio::async_read(*(this->socket_), asio::buffer(&data_length, 4), asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            SPDLOG_WARN("{}", ec.value());
            this->stop();
            co_return;
        }

        data_length = asio::detail::socket_ops::network_to_host_long(data_length);
        request_data.resize(data_length);

        co_await asio::async_read(*(this->socket_), asio::buffer(request_data.data(), request_data.length()), asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            SPDLOG_WARN("{}", ec.value());
            this->stop();
            co_return;
        }

        SPDLOG_INFO("async_read");

        /* 将消息放到队列中去, 所有的服务执行操作全部都在上下文的一个特定协程中执行, 保证同步 */
        coro_actor_msg_t msg;
        msg.service_name = std::move(service_name);
        msg.pack = coro_actor_pack_t { .type = type, .value = std::move(request_data) };
        msg.socket = this->socket_;

        g_actor_ctx->push_msg(std::move(msg));
    }
}