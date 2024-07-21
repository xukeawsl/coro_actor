#pragma once

#include "coro_actor_common.h"
#include "coro_actor_config.h"
#include "coro_actor_protocol.h"

#ifndef ACTOR_PROC_NAME
#define ACTOR_PROC_NAME "coro_actor"
#endif

class coro_actor {
public:
    enum class PTYPE : uint8_t {
        MASTER,
        WORKER,
        PROXIER
    };

public:
    /*接受一个 yaml 文件作为启动配置*/
    coro_actor(int argc, char *argv[]);

    ~coro_actor();

    void run() noexcept;

private:
    struct proxy_process {
        pid_t pid = -1;
        bool exited = true;
    };

    struct actor_process {
        /*actor进程id*/
        pid_t pid = -1;

        /*进程是否处于退出状态*/
        bool exited = true;

        /*actor 节点名称*/
        std::string name;
    };
    
    struct dlhandle_deleter {
        void operator()(void *handle) const {
            dlclose(handle);
        }
    };

    /* proxy 进程使用, 记录一个 actor 连接的信息*/
    struct actor_info {
        /* actor 节点名 */
        std::string name;
        
        /* actor 进程 id */
        pid_t pid = -1;

        /* 是否等待响应 */
        bool is_waiting = false;

        /* 调用 id, 用于唯一标识报文 */
        std::string call_id;

        /* 缓存消息队列, 当 actor 进程在等待响应时不能将非响应的消息发送给它 */
        std::queue<coro_actor_request_t> msg_queue;

        /* 与对应 actor 进程的连接 */
        std::shared_ptr<asio::local::stream_protocol::socket> socket;
    };

private:
    void stop_server();

    void init();

    /* 更新所有进程的状态 */
    void update_processes_status();

    /* 初始化代理进程 */
    void init_proxy_process();

    /* 重新拉起 proxy 进程 */
    void reap_proxy_process();

    /* 停止 proxy 进程 */
    void stop_proxy_process();

    /* proxy 进程处理的逻辑 */
    void handle_proxy_process();

    asio::awaitable<void> handle_proxy_accept(asio::local::stream_protocol::acceptor& acceptor);

    asio::awaitable<void> handle_proxy_deal_actor(std::shared_ptr<asio::local::stream_protocol::socket> socket);

    asio::awaitable<void> handle_proxy_handle_request(std::shared_ptr<actor_info> af);

    /* 初始化并启动 actor 进程 */
    void init_actor_processes();

    /* 重新拉起退出的 actor 进程 */
    void reap_actor_processes();

    /* 停止所有 actor 进程 */
    void stop_actor_processes();

    /* fork 出单个 actor 进程 */
    void spawn_actor_process(const std::string& actor_name, int respawn);

    /* actor 进程的处理逻辑 */
    void handle_actor_process();

    /* actor 进程通知 proxy 进程 */
    asio::awaitable<void> handle_actor_notify_proxy();

    /* acotr 进程处理服务间调用 */
    asio::awaitable<void> handle_actor_internal(std::shared_ptr<asio::local::stream_protocol::socket> unix_socket);

    /* actor 进程处理远程调用 */
    asio::awaitable<void> handle_actor_remote();

    bool set_daemon();

    void init_setproctitle();

    void set_proctitle(const std::string& title);

    void set_logger(const std::string& logger_name);

    void signal_handler();

    void update_actor_info(std::shared_ptr<actor_info> af, const std::string& call_id, bool is_waiting);

    /* 打印指定服务和调用 id 对应的调用链 */
    void look_call_chain(const std::string& call_id, const std::string& service_name);

private:
    PTYPE type;

    int os_argc;
    char **os_argv;
    char *os_argv_last;

    std::shared_ptr<char[]> os_environ;

    asio::io_context io_context;
    asio::signal_set signals;

    std::string unix_fd;

    /* 当前 actor 进程的节点名称 */
    std::string actor_name;

    /* 服务名和提供服务的 actor 节点名 */
    std::unordered_map<std::string, std::string> service_map;

    /* actor 节点名和其对应的配置 */
    std::unordered_map<std::string, actor_config> actor_config_map;

    /* actor 节点名和其需要监听的端点 */
    std::unordered_map<std::string, std::shared_ptr<asio::ip::tcp::acceptor>> ac_map;

    /* dlopen 句柄 */
    std::unique_ptr<void, dlhandle_deleter> dl_handle;

    bool terminate;
    bool isdaemon;

    proxy_process proxy;
    std::vector<actor_process> actor_processes;

    std::string log_dir;

    /* proxy 专用数据成员 */
    std::unordered_map<std::string, std::list<std::shared_ptr<actor_info>>> actor_info_map;

    /* 通过 pid 直接找到 actor_info */
    std::unordered_map<pid_t, std::shared_ptr<actor_info>> actor_pid_map;

    /* 根据 call_id 直接取出调用链 */
    std::unordered_map<std::string, std::list<std::shared_ptr<actor_info>>> actor_call_chain_map;
};