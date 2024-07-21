#include "coro_actor_ctx.h"

coro_actor_ctx::coro_actor_ctx(
    asio::io_context& io_ctx,
    std::shared_ptr<asio::local::stream_protocol::socket> unix_socket,
    std::unordered_map<std::string, coro_actor_ctx::func_t> service_map)
    : io_ctx_(io_ctx),
      unix_socket_(unix_socket),
      pid_(getpid()),
      seq_(0),
      service_map_(std::move(service_map)),
      msg_notify_timer_(io_ctx_),
      service_notify_timer_(io_ctx_),
      response_notify_tiemr_(io_ctx_),
      is_waiting(false) {
    this->msg_notify_timer_.expires_at(
        std::chrono::steady_clock::time_point::max());
    this->service_notify_timer_.expires_at(
        std::chrono::steady_clock::time_point::max());
    this->response_notify_tiemr_.expires_at(
        std::chrono::steady_clock::time_point::max());

    asio::co_spawn(this->io_ctx_, handle_message(), asio::detached);
}

asio::io_context& coro_actor_ctx::io_contex() { return this->io_ctx_; }

bool coro_actor_ctx::contains(const std::string& service_name) {
    return this->service_map_.contains(service_name);
}

void coro_actor_ctx::set_call_id(const std::string& call_id) {
    this->call_id_ = call_id;
}

std::string coro_actor_ctx::get_error_msg() const { return this->error_msg_; }

void coro_actor_ctx::set_error_msg(const std::string& msg) {
    this->error_msg_ = msg;
}

asio::awaitable<bool> coro_actor_ctx::send(
    const std::string& service_name, const coro_actor_pack_t* request_pack,
    coro_actor_pack_t* response_pack) {
    /* 检查服务是否在本 actor 进程提供的列表中 */
    if (this->contains(service_name)) {
        /* 在上下文设置错误信息, 记录错误日志 */
        this->set_error_msg(
            "This service is already supported by this process");
        co_return false;
    }

    /* 为空说明当前节点为调用链的起始节点 */
    if (this->call_id_.empty()) {
        this->call_id_ = std::to_string(pid_) + "-" + std::to_string(++seq_);
    }

    asio::error_code ec;

    /* 将请求发送给 proxy 进程, 由它转发到相应的进程执行 */
    coro_actor_request_t req;
    req.pid = this->pid_;
    req.call_id = this->call_id_;
    req.service_name = service_name;
    req.request_pack = *request_pack;

    std::string serialize_data = struct_pack::serialize<std::string>(req);

    uint8_t proto = CORO_ACTOR_REQUEST;
    uint32_t serialize_length = serialize_data.length();
    serialize_length =
        asio::detail::socket_ops::host_to_network_long(serialize_length);

    std::array<asio::const_buffer, 3> write_buf = {
        {asio::buffer(&proto, 1), asio::buffer(&serialize_length, 4),
         asio::buffer(serialize_data.data(), serialize_data.length())}};

    co_await asio::async_write(*(this->unix_socket_), write_buf,
                               asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
        this->error_msg_ = ec.value();
        co_return false;
    }

    /* 等待响应 */
    co_await this->response_notify_tiemr_.async_wait(
        asio::redirect_error(asio::use_awaitable, ec));

    *response_pack = std::move(this->curr_resp_pack_.response_pack);

    co_return true;
}

void coro_actor_ctx::push_msg(coro_actor_msg_t&& msg) {
    this->msg_queue_.push(msg);

    /* 应该通知工作协程从队列中取消息处理, 优化: 只有当队列只有一条消息时才通知
     */
    if (this->msg_queue_.size() == 1) {
        this->msg_notify_timer_.cancel_one();
    }
}

void coro_actor_ctx::notify_complete() {
    this->is_waiting = false;
    this->call_id_.clear();
    this->service_notify_timer_.cancel_one();
}

void coro_actor_ctx::set_response_pack(coro_actor_response_t&& resp_pack) {
    this->curr_resp_pack_ = std::move(resp_pack);
    this->response_notify_tiemr_.cancel_one();
}

asio::awaitable<void> coro_actor_ctx::handle_message() {
    asio::error_code ec;
    uint32_t length;

    while (!this->io_ctx_.stopped()) {
        if (this->msg_queue_.empty()) {
            co_await this->msg_notify_timer_.async_wait(
                asio::redirect_error(asio::use_awaitable, ec));
        } else {
            auto msg = this->msg_queue_.front();
            this->msg_queue_.pop();

            if (msg.socket && !msg.socket->is_open()) continue;
            if (msg.unix_socket && !msg.unix_socket->is_open()) continue;

            if (!this->service_map_.contains(msg.service_name)) {
                SPDLOG_INFO("not found service: {}", msg.service_name);
                msg.socket->close(ec);
                continue;
            }

            /* 调用 ID 传递 */
            this->call_id_ = msg.call_id;

            /* 调用服务进行处理 */
            auto func = this->service_map_[msg.service_name];

            coro_actor_pack_t resp_pack;

            this->is_waiting = true;

            func(&msg.pack, &resp_pack);

            if (this->is_waiting) {
                co_await this->service_notify_timer_.async_wait(
                    asio::redirect_error(asio::use_awaitable, ec));
            }

            /* 如果调用失败则断开连接, 继续处理下一个消息 */
            if (!this->error_msg_.empty()) {
                this->error_msg_.clear();

                if (msg.socket) msg.socket->close(ec);
                if (msg.unix_socket) msg.unix_socket->close(ec);

                continue;
            }

            /* 调用成功则发送响应消息 */
            if (msg.socket) {
                length = asio::detail::socket_ops::host_to_network_long(
                    resp_pack.value.length());

                std::array<asio::const_buffer, 3> write_buf = {
                    {asio::buffer(&resp_pack.type, 1), asio::buffer(&length, 4),
                     asio::buffer(resp_pack.value.data(),
                                  resp_pack.value.length())}};

                co_await asio::async_write(
                    *msg.socket, write_buf,
                    asio::redirect_error(asio::use_awaitable, ec));
            } else if (msg.unix_socket) {
                coro_actor_response_t pack;
                pack.pid = this->pid_;
                pack.call_id = std::move(msg.call_id);
                pack.service_name = std::move(msg.service_name);
                pack.response_pack = std::move(resp_pack);

                uint8_t proto = CORO_ACTOR_RESPONSE;

                auto serialize_pack = struct_pack::serialize<std::string>(pack);

                length = asio::detail::socket_ops::host_to_network_long(
                    serialize_pack.length());

                std::array<asio::const_buffer, 3> write_buf = {
                    {asio::buffer(&proto, 1), asio::buffer(&length, 4),
                     asio::buffer(serialize_pack.data(),
                                  serialize_pack.length())}};

                co_await asio::async_write(
                    *msg.unix_socket, write_buf,
                    asio::redirect_error(asio::use_awaitable, ec));
            }
        }
    }

    co_return;
}