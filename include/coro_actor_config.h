#pragma once

#include "coro_actor_common.h"

struct actor_config {
    /* actor 节点名称 */
    std::string name;

    /* 启动的进程数 */
    uint32_t count;

    /* 使用哪个动态库启动 */
    std::string lib;

    /* 监听的地址 */
    std::string address;

    /* 监听的端口 */
    uint16_t port;

    /* 提供的服务 */
    std::unordered_set<std::string> services;
};

class coro_actor_config {
public:
    static coro_actor_config* get();

    bool load(const std::string& file);

    inline bool is_daemon() const { return this->daemon_; }

    inline std::string get_unix() const { return this->unix_; }

    inline std::unordered_map<std::string, std::string> get_service_map() const { return this->service_map_; }

    inline std::unordered_map<std::string, actor_config> get_actor_config_map() const { return this->actor_config_map_; }

private:
    coro_actor_config();
    ~coro_actor_config() = default;
    coro_actor_config(const coro_actor_config&) = delete;
    coro_actor_config& operator=(const coro_actor_config&) = delete;
    coro_actor_config(coro_actor_config&&) = delete;
    coro_actor_config& operator=(coro_actor_config&&) = delete;

private:
    bool daemon_;

    std::string unix_;

    /* 根据服务名查找对应的 actor 节点名 */
    std::unordered_map<std::string, std::string> service_map_;

    /* 根据 actor 节点名查找对应的配置 */
    std::unordered_map<std::string, actor_config> actor_config_map_;
};