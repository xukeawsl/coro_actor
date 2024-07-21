#include "coro_actor.h"

#include "coro_actor_ctx.h"
#include "coro_actor_protocol.h"
#include "coro_actor_session.h"

extern char** environ;

coro_actor_ctx* g_actor_ctx;

static char* cpystrn(char* dst, const char* src, size_t n) {
    if (n == 0) {
        return dst;
    }

    while (--n) {
        *dst = *src;
        if (*dst == '\0') {
            return dst;
        }

        dst++;
        src++;
    }

    *dst = '\0';

    return dst;
}

coro_actor::coro_actor(int argc, char* argv[])
    : type(PTYPE::MASTER),
      os_argc(argc),
      os_argv(argv),
      os_argv_last(argv[0]),
      io_context(1),
      signals(io_context),
      terminate(false),
      isdaemon(false) {}

coro_actor::~coro_actor() { spdlog::shutdown(); }

void coro_actor::run() noexcept {
    try {
        this->init();

        this->init_proxy_process();

        this->init_actor_processes();

        this->io_context.run();

        SPDLOG_INFO("{} stop", ACTOR_PROC_NAME);
    } catch (std::exception& e) {
        SPDLOG_ERROR("{} failed to run : {}", ACTOR_PROC_NAME,
                     std::string(e.what()));
    }
}

void coro_actor::stop_server() {
    this->terminate = true;
    this->io_context.stop();
}

void coro_actor::init() {
    if (this->os_argc != 2) {
        throw std::runtime_error(
            "Need a config file. usage: coro_actor configfilename");
    }

    /* 加载配置文件 */
    if (!coro_actor_config::get()->load(os_argv[1])) {
        throw std::runtime_error("Failed to load config file.");
    }

    this->unix_fd = coro_actor_config::get()->get_unix();
    this->service_map = coro_actor_config::get()->get_service_map();
    this->actor_config_map = coro_actor_config::get()->get_actor_config_map();

    if (this->isdaemon) {
        if (!this->set_daemon()) {
            exit(EXIT_FAILURE);
        }
    }

    this->init_setproctitle();
    this->set_proctitle("master");

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;

    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S",
                  std::localtime(&time));
    ss << buffer;
    this->log_dir = "logs/" + ss.str() + "/";

    this->set_logger("actor_master");

    this->signals.add(SIGINT);
    this->signals.add(SIGTERM);
    this->signals.add(SIGCHLD);

    this->signal_handler();

    for (const auto& [_, cfg] : this->actor_config_map) {
        for (auto i = 0; i < cfg.count; i++) {
            this->actor_processes.emplace_back(actor_process{.name = cfg.name});
        }

        if (cfg.address.empty()) continue;

        auto listen_endpoint = asio::ip::tcp::endpoint(
            asio::ip::make_address(cfg.address), cfg.port);
        auto acceptor =
            std::make_shared<asio::ip::tcp::acceptor>(this->io_context);
        acceptor->open(listen_endpoint.protocol());
        acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true));
        acceptor->bind(listen_endpoint);
        acceptor->listen();

        ac_map[cfg.name] = acceptor;
    }
}

bool coro_actor::set_daemon() {
    this->io_context.notify_fork(asio::io_context::fork_prepare);

    if (pid_t pid = fork()) {
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        } else {
            return false;
        }
    }

    if (setsid() == -1) {
        return false;
    }

    umask(0);

    int fd = open("/dev/null", O_RDWR);
    if (fd == -1) {
        return false;
    }

    if (dup2(fd, STDIN_FILENO) == -1) {
        return false;
    }

    if (dup2(fd, STDOUT_FILENO) == -1) {
        return false;
    }

    if (fd > STDERR_FILENO) {
        if (close(fd) == -1) {
            return false;
        }
    }

    this->io_context.notify_fork(asio::io_context::fork_child);

    return true;
}

void coro_actor::init_setproctitle() {
    size_t size = 0;

    for (int i = 0; environ[i]; i++) {
        size += strlen(environ[i]) + 1;
    }

    this->os_environ.reset(new char[size]);

    for (int i = 0; this->os_argv[i]; i++) {
        if (this->os_argv_last == this->os_argv[i]) {
            this->os_argv_last =
                this->os_argv[i] + strlen(this->os_argv[i]) + 1;
        }
    }

    char* p = this->os_environ.get();

    for (int i = 0; environ[i]; i++) {
        if (this->os_argv_last == environ[i]) {
            size = strlen(environ[i]) + 1;
            this->os_argv_last = environ[i] + size;

            cpystrn(p, environ[i], size);
            environ[i] = p;

            p += size;
        }
    }

    this->os_argv_last--;
}

void coro_actor::set_proctitle(const std::string& title) {
    char* p = nullptr;

    this->os_argv[1] = nullptr;

    p = cpystrn(this->os_argv[0], ACTOR_PROC_NAME,
                this->os_argv_last - this->os_argv[0]);

    p = cpystrn(p, ": ", this->os_argv_last - p);
    p = cpystrn(p, title.c_str(), this->os_argv_last - p);

    if (this->os_argv_last - p) {
        memset(p, '\0', this->os_argv_last - p);
    }
}

void coro_actor::set_logger(const std::string& logger_name) {
    std::string log_file =
        this->log_dir + logger_name + "-" + std::to_string(getpid()) + ".log";

    if (this->type == PTYPE::WORKER) {
        spdlog::init_thread_pool(8192, 1);

        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, 1048576 * 5, 3);

        spdlog::set_default_logger(std::make_shared<spdlog::async_logger>(
            logger_name, file_sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::block));
    } else {
        auto file_logger =
            spdlog::rotating_logger_st(logger_name, log_file, 1048576 * 5, 3);

        spdlog::set_default_logger(file_logger);
    }

    spdlog::set_pattern("[%Y-%m-%d %T.%f] [thread %t] [%^%l%$] [%s:%#] %v");

    switch (SPDLOG_ACTIVE_LEVEL) {
        case SPDLOG_LEVEL_TRACE:
            spdlog::set_level(spdlog::level::trace);
            break;
        case SPDLOG_LEVEL_DEBUG:
            spdlog::set_level(spdlog::level::debug);
            break;
        case SPDLOG_LEVEL_INFO:
            spdlog::set_level(spdlog::level::info);
            break;
        case SPDLOG_LEVEL_WARN:
            spdlog::set_level(spdlog::level::warn);
            break;
        case SPDLOG_LEVEL_ERROR:
            spdlog::set_level(spdlog::level::err);
            break;
        case SPDLOG_LEVEL_CRITICAL:
            spdlog::set_level(spdlog::level::critical);
            break;
        case SPDLOG_LEVEL_OFF:
            spdlog::set_level(spdlog::level::off);
            break;
        default:
            break;
    }
}

void coro_actor::signal_handler() {
    this->signals.async_wait([this](asio::error_code ec, int sig) {
        if (ec) {
            return;
        }

        this->signal_handler();

        switch (sig) {
            case SIGINT:
            case SIGTERM: {
                if (this->type == PTYPE::MASTER) {
                    this->stop_proxy_process();
                    this->stop_actor_processes();
                }

                this->stop_server();

                break;
            }
            case SIGCHLD: {
                this->update_processes_status();

                if (!this->terminate) {
                    this->reap_proxy_process();
                    this->reap_actor_processes();
                }

                break;
            }
            default: {
                break;
            }
        }
    });
}

void coro_actor::update_actor_info(std::shared_ptr<actor_info> af,
                                   const std::string& call_id,
                                   bool is_waiting) {
    af->call_id = call_id;
    af->is_waiting = is_waiting;
}

void coro_actor::look_call_chain(const std::string& call_id,
                                 const std::string& service_name) {
    int order = 1;
    for (auto af : this->actor_call_chain_map[call_id]) {
        SPDLOG_INFO(
            "order = {}, service = {}, call_id = {}, name = {}, pid = {}, "
            "waiting = {}",
            order++, service_name, af->call_id, af->name, af->pid,
            af->is_waiting);
    }
}

void coro_actor::init_proxy_process() {
    this->io_context.notify_fork(asio::io_context::fork_prepare);
    pid_t pid = fork();

    switch (pid) {
        case -1: {
            return;
        }
        case 0: {
            this->io_context.notify_fork(asio::io_context::fork_child);
            this->set_proctitle("proxy");
            this->set_logger("actor_proxy");
            this->handle_proxy_process();
            break;
        }
        default: {
            this->io_context.notify_fork(asio::io_context::fork_parent);
            break;
        }
    }

    this->proxy.pid = pid;
}

void coro_actor::reap_proxy_process() {
    if (this->type != PTYPE::PROXIER) return;

    if (this->proxy.pid != -1 && this->proxy.exited) {
        this->init_proxy_process();
    }
}

void coro_actor::stop_proxy_process() {
    if (this->proxy.pid != -1) {
        kill(this->proxy.pid, SIGTERM);
    }
}

void coro_actor::handle_proxy_process() {
    std::remove(this->unix_fd.c_str());

    asio::local::stream_protocol::acceptor acceptor(
        this->io_context,
        asio::local::stream_protocol::endpoint(this->unix_fd));

    asio::co_spawn(acceptor.get_executor(), handle_proxy_accept(acceptor),
                   asio::detached);

    this->io_context.run();

    exit(EXIT_SUCCESS);
}

asio::awaitable<void> coro_actor::handle_proxy_accept(
    asio::local::stream_protocol::acceptor& acceptor) {
    for (;;) {
        asio::error_code ec;

        auto socket = std::make_shared<asio::local::stream_protocol::socket>(
            this->io_context);
        co_await acceptor.async_accept(
            *socket, asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            co_return;
        }

        asio::co_spawn(acceptor.get_executor(), handle_proxy_deal_actor(socket),
                       asio::detached);
    }
}

asio::awaitable<void> coro_actor::handle_proxy_deal_actor(
    std::shared_ptr<asio::local::stream_protocol::socket> socket) {
    asio::error_code ec;
    uint8_t proto;
    uint32_t length;
    std::string deserialize_pack;

    for (;;) {
        co_await asio::async_read(
            *socket, asio::buffer(&proto, 1),
            asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            co_return;
        }

        co_await asio::async_read(
            *socket, asio::buffer(&length, 4),
            asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            co_return;
        }

        length = asio::detail::socket_ops::network_to_host_long(length);

        deserialize_pack.resize(length);

        co_await asio::async_read(
            *socket,
            asio::buffer(deserialize_pack.data(), deserialize_pack.length()),
            asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            /* 有可能是上一次发送了 request 包但是被调用方崩溃了, 导致连接断开
             */
            co_return;
        }

        switch (proto) {
            case CORO_ACTOR_CONNECT: {
                auto pack_opt = struct_pack::deserialize<coro_actor_connect_t>(
                    deserialize_pack);
                assert(pack_opt.has_value());

                auto pack = pack_opt.value();

                SPDLOG_INFO("CONNECT: pid = [{}], name = [{}]", pack.pid,
                            pack.name);

                auto af = std::make_shared<actor_info>();
                af->name = pack.name;
                af->pid = pack.pid;
                af->socket = socket;

                this->actor_pid_map[af->pid] = af;
                this->actor_info_map[pack.name].emplace_back(af);

                coro_actor_connack_t ack_pack;
                ack_pack.result = true;

                proto = CORO_ACTOR_CONNACK;

                std::string serialize_pack =
                    struct_pack::serialize<std::string>(ack_pack);

                length = asio::detail::socket_ops::host_to_network_long(
                    serialize_pack.length());

                std::array<asio::const_buffer, 3> write_buf = {
                    {asio::buffer(&proto, 1), asio::buffer(&length, 4),
                     asio::buffer(serialize_pack.data(),
                                  serialize_pack.length())}};

                co_await asio::async_write(
                    *socket, write_buf,
                    asio::redirect_error(asio::use_awaitable, ec));

                break;
            }
            case CORO_ACTOR_REQUEST: {
                auto pack_opt = struct_pack::deserialize<coro_actor_request_t>(
                    deserialize_pack);
                assert(pack_opt.has_value());

                auto pack = pack_opt.value();

                auto af = this->actor_pid_map[pack.pid];

                /* 如果是第一个节点则加入调用链 */
                if (!this->actor_call_chain_map.contains(pack.call_id)) {
                    this->actor_call_chain_map[pack.call_id].emplace_back(af);
                }

                af->msg_queue.emplace(std::move(pack));

                co_await this->handle_proxy_handle_request(af);

                break;
            }
            case CORO_ACTOR_RESPONSE: {
                auto pack_opt = struct_pack::deserialize<coro_actor_response_t>(
                    deserialize_pack);
                assert(pack_opt.has_value());

                auto pack = pack_opt.value();

                proto = CORO_ACTOR_RESPONSE;

                std::string serialize_pack =
                    struct_pack::serialize<std::string>(pack);

                length = asio::detail::socket_ops::host_to_network_long(
                    serialize_pack.length());

                std::array<asio::const_buffer, 3> write_buf = {
                    {asio::buffer(&proto, 1), asio::buffer(&length, 4),
                     asio::buffer(serialize_pack.data(),
                                  serialize_pack.length())}};

                this->look_call_chain(pack.call_id, pack.service_name);

                auto& call_chain = this->actor_call_chain_map[pack.call_id];

                auto af_self = call_chain.back();
                call_chain.pop_back();

                co_await asio::async_write(
                    *(call_chain.back()->socket), write_buf,
                    asio::redirect_error(asio::use_awaitable, ec));

                this->update_actor_info(af_self, "", false);
                co_await this->handle_proxy_handle_request(af_self);

                this->update_actor_info(call_chain.back(), pack.call_id, false);
                co_await this->handle_proxy_handle_request(call_chain.back());
            }
        }
    }
}

asio::awaitable<void> coro_actor::handle_proxy_handle_request(
    std::shared_ptr<actor_info> af) {
    asio::error_code ec;

    if (af->msg_queue.empty() || af->is_waiting) {
        co_return;
    }

    /* 这里别用引用, 注意 pop 后引用失效的问题 */
    auto pack = af->msg_queue.front();
    af->msg_queue.pop();

    /* 根据服务名找到 actor 进程组 */
    auto it = this->service_map.find(pack.service_name);
    if (it == this->service_map.end()) {
        SPDLOG_INFO("not support {}", pack.service_name);
        co_return;
    }

    auto actor_name = it->second;

    auto& actor_info_list = this->actor_info_map[actor_name];
    if (actor_info_list.empty()) {
        /* 支持服务的节点都断开了 */
        co_return;
    }

    auto select_iter = actor_info_list.begin();
    auto min_que_size = (*select_iter)->msg_queue.size();

    for (auto info_iter = actor_info_list.begin();
         info_iter != actor_info_list.end();) {
        if (!(*info_iter)->socket->is_open()) {
            this->actor_pid_map.erase((*info_iter)->pid);
            info_iter = actor_info_list.erase(info_iter);
        } else {
            if ((*info_iter)->call_id == pack.call_id) {
                /* 存在循环调用 */
                co_return;
            }

            /* 选择 actor 进程组中内部消息队列积压最少的一个进程进行处理 */
            if ((*info_iter)->msg_queue.size() < min_que_size) {
                select_iter = info_iter;
                min_que_size = (*select_iter)->msg_queue.size();
            }

            info_iter++;
        }
    }

    /* 将发起请求一方的状态更新为等待中 */
    this->update_actor_info(this->actor_pid_map[pack.pid], pack.call_id, true);

    /* 接收方加入调用链 */
    this->actor_call_chain_map[pack.call_id].emplace_back(*select_iter);

    /* 接收请求方如果已经进入等待状态了, 则将消息缓存在队列中 */
    if ((*select_iter)->is_waiting) {
        (*select_iter)->msg_queue.emplace(std::move(pack));
        co_return;
    }

    this->update_actor_info(*select_iter, pack.call_id, false);

    uint8_t proto = CORO_ACTOR_REQUEST;

    std::string serialize_pack = struct_pack::serialize<std::string>(pack);

    uint32_t length =
        asio::detail::socket_ops::host_to_network_long(serialize_pack.length());

    std::array<asio::const_buffer, 3> write_buf = {
        {asio::buffer(&proto, 1), asio::buffer(&length, 4),
         asio::buffer(serialize_pack.data(), serialize_pack.length())}};

    co_await asio::async_write(*((*select_iter)->socket), write_buf,
                               asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
        /* 解除等待状态 */
        this->update_actor_info(this->actor_pid_map[pack.pid], "", false);
        this->update_actor_info(*select_iter, "", false);
        co_return;
    }
}

void coro_actor::spawn_actor_process(const std::string& actor_name,
                                     int respawn) {
    this->io_context.notify_fork(asio::io_context::fork_prepare);
    pid_t pid = fork();

    switch (pid) {
        case -1: {
            return;
        }
        case 0: {
            this->io_context.notify_fork(asio::io_context::fork_child);
            this->type = PTYPE::WORKER;
            this->actor_name = actor_name;
            this->set_proctitle("actor_" + actor_name);
            this->set_logger("actor_node_" + actor_name);
            this->handle_actor_process();
            break;
        }
        default: {
            this->io_context.notify_fork(asio::io_context::fork_parent);
            break;
        }
    }

    this->actor_processes[respawn].pid = pid;
    this->actor_processes[respawn].exited = false;
}

void coro_actor::handle_actor_process() {
    asio::co_spawn(this->io_context, handle_actor_notify_proxy(),
                   asio::detached);

    this->io_context.run();

    exit(EXIT_SUCCESS);
}

asio::awaitable<void> coro_actor::handle_actor_notify_proxy() {
    /* 根据配置项加载动态库 */
    auto cfg = this->actor_config_map[this->actor_name];

    this->dl_handle.reset(dlopen(cfg.lib.c_str(), RTLD_LAZY | RTLD_GLOBAL));

    if (!dl_handle) {
        SPDLOG_ERROR("failed to dlopen - {}", dlerror());
        this->stop_server();
        co_return;
    }

    /* 将加载好的服务列表设置到上下文中 */
    std::unordered_map<std::string, coro_actor_ctx::func_t> service_map;
    for (const auto& service : cfg.services) {
        auto service_func = reinterpret_cast<coro_actor_ctx::func_t>(
            dlsym(dl_handle.get(), service.c_str()));
        if (!service_func) {
            SPDLOG_ERROR("failed to dlsym - {}", dlerror());
            this->stop_server();
            co_return;
        }
        service_map[service] = service_func;
    }

    asio::error_code ec;
    auto unix_socket = std::make_shared<asio::local::stream_protocol::socket>(
        this->io_context);

    co_await unix_socket->async_connect(
        asio::local::stream_protocol::endpoint(this->unix_fd),
        asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
        SPDLOG_INFO("{}", ec.value());
        this->stop_server();
        co_return;
    }

    coro_actor_connect_t pack;
    pack.pid = getpid();
    pack.name = this->actor_name;

    auto serialize_pack = struct_pack::serialize<std::string>(pack);

    uint8_t proto = CORO_ACTOR_CONNECT;
    uint32_t serialize_length = serialize_pack.length();
    serialize_length =
        asio::detail::socket_ops::host_to_network_long(serialize_length);

    std::array<asio::const_buffer, 3> write_buf = {
        {asio::buffer(&proto, 1), asio::buffer(&serialize_length, 4),
         asio::buffer(serialize_pack.data(), serialize_pack.length())}};

    co_await asio::async_write(*unix_socket, write_buf,
                               asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
        SPDLOG_INFO("{}", ec.value());
        co_return;
    }

    std::array<asio::mutable_buffer, 2> read_buf = {
        {asio::buffer(&proto, 1), asio::buffer(&serialize_length, 4)}};

    co_await asio::async_read(*unix_socket, read_buf,
                              asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
        SPDLOG_INFO("{}", ec.value());
        co_return;
    }

    assert(proto == CORO_ACTOR_CONNACK);

    serialize_length =
        asio::detail::socket_ops::network_to_host_long(serialize_length);

    serialize_pack.resize(serialize_length);
    co_await asio::async_read(
        *unix_socket,
        asio::buffer(serialize_pack.data(), serialize_pack.length()),
        asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
        SPDLOG_INFO("{}", ec.value());
        co_return;
    }

    auto ack_pack_opt =
        struct_pack::deserialize<coro_actor_connack_t>(serialize_pack);
    assert(ack_pack_opt.has_value());

    auto ack_result = ack_pack_opt.value().result;
    if (!ack_result) {
        co_return;
    }

    g_actor_ctx = new coro_actor_ctx(this->io_context, unix_socket,
                                     std::move(service_map));

    /* 开启两个协程, 一个用于接收 unix 域套接字的请求,
     * 一个用于接收远程客户端的请求 */
    asio::co_spawn(this->io_context, handle_actor_internal(unix_socket),
                   asio::detached);
    asio::co_spawn(this->io_context, handle_actor_remote(), asio::detached);
}

asio::awaitable<void> coro_actor::handle_actor_internal(
    std::shared_ptr<asio::local::stream_protocol::socket> unix_socket) {
    asio::error_code ec;
    uint8_t proto;
    uint32_t length;
    std::string deserialize_pack;

    while (unix_socket->is_open()) {
        co_await asio::async_read(
            *unix_socket, asio::buffer(&proto, 1),
            asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            co_return;
        }

        co_await asio::async_read(
            *unix_socket, asio::buffer(&length, 4),
            asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            co_return;
        }

        length = asio::detail::socket_ops::network_to_host_long(length);

        deserialize_pack.resize(length);

        co_await asio::async_read(
            *unix_socket,
            asio::buffer(deserialize_pack.data(), deserialize_pack.length()),
            asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            co_return;
        }

        switch (proto) {
            case CORO_ACTOR_REQUEST: {
                auto pack_opt = struct_pack::deserialize<coro_actor_request_t>(
                    deserialize_pack);
                assert(pack_opt.has_value());

                auto pack = pack_opt.value();

                coro_actor_msg_t msg;
                msg.service_name = std::move(pack.service_name);
                msg.call_id = std::move(pack.call_id);
                msg.pack = std::move(pack.request_pack);
                msg.unix_socket = unix_socket;

                g_actor_ctx->set_call_id(pack.call_id);
                g_actor_ctx->push_msg(std::move(msg));

                break;
            }
            case CORO_ACTOR_RESPONSE: {
                auto pack_opt = struct_pack::deserialize<coro_actor_response_t>(
                    deserialize_pack);
                assert(pack_opt.has_value());

                g_actor_ctx->set_response_pack(std::move(pack_opt.value()));

                break;
            }
        }
    }
}

asio::awaitable<void> coro_actor::handle_actor_remote() {
    /* 监听在一个预设的地址, 如果没有则只支持服务内部调用 */
    auto it = this->ac_map.find(this->actor_name);
    if (it == this->ac_map.end()) {
        SPDLOG_INFO("{}", this->actor_name);
        co_return;
    }

    asio::error_code ec;
    auto acceptor = it->second;

    for (;;) {
        asio::ip::tcp::socket socket(this->io_context);
        co_await acceptor->async_accept(
            socket, asio::redirect_error(asio::use_awaitable, ec));

        if (ec) {
            SPDLOG_WARN("{}", ec.value());
            co_return;
        }

        std::make_shared<coro_actor_session>(std::move(socket))->start();
    }
}

void coro_actor::init_actor_processes() {
    int n = this->actor_processes.size();

    for (int i = 0; i < n; i++) {
        int idx = -1;

        for (int j = 0; j < n; j++) {
            if (this->actor_processes[j].pid == -1) {
                idx = j;
                break;
            }
        }

        if (idx == -1) {
            break;
        }

        this->spawn_actor_process(this->actor_processes[idx].name, idx);
    }
}

void coro_actor::stop_actor_processes() {
    for (auto& p : this->actor_processes) {
        if (p.pid == -1) {
            continue;
        }

        kill(p.pid, SIGTERM);
    }
}

void coro_actor::update_processes_status() {
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid == 0) {
            return;
        }

        if (pid == -1) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }

        if (this->proxy.pid == pid) {
            this->proxy.exited = true;
        }

        for (auto& p : this->actor_processes) {
            if (p.pid == pid) {
                p.exited = true;
            }
        }
    }
}

void coro_actor::reap_actor_processes() {
    if (this->type != PTYPE::WORKER) return;

    int n = this->actor_processes.size();

    for (int idx = 0; idx < n; idx++) {
        if (this->actor_processes[idx].pid == -1) {
            continue;
        }

        if (this->actor_processes[idx].exited) {
            this->spawn_actor_process(this->actor_processes[idx].name, idx);
        }
    }
}