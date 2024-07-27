#include "coro_actor_config.h"

#include "yaml-cpp/yaml.h"

/* 配置解析的内容 */
/* 模块的详细配置项, 每一项描述加载的 Actor 节点相关信息 */
/* 对于每个 Actor 节点, 需要解析节点名、模块文件名、描述具体服务名的配置文件,
 * 可选的监听地址, 表示服务对外开放 */
/* 注意不同 Actor 节点不能配置相同的监听地址且所有 Actor
 * 提供的服务不能存在重复即服务名必须全局唯一 */

coro_actor_config* coro_actor_config::get() {
    static coro_actor_config instance;
    return &instance;
}

coro_actor_config::coro_actor_config()
    : daemon_(false), pid_file_("/run/coro_actor.pid") {}

bool coro_actor_config::load(const std::string& file) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(file);

        if (root["daemon"].IsDefined()) {
            this->daemon_ = root["daemon"].as<bool>();
        }

        this->unix_ = root["unix"].as<std::string>();

        if (root["pid"].IsDefined()) {
            this->pid_file_ = root["pid"].as<std::string>();
        }

        /* 检查是否存在同名 actor 节点*/
        std::unordered_set<std::string> check_actor_name;

        /* 检查是否存在同名服务 */
        std::unordered_set<std::string> check_service;

        for (const auto& actor_detail : root["actor_config"]) {
            if (!actor_detail["actor_name"].IsDefined()) {
                throw std::runtime_error("'actor_name' must be set");
            }
            if (!actor_detail["actor_count"].IsDefined()) {
                throw std::runtime_error("'actor_count' must be set");
            }
            if (!actor_detail["actor_module"].IsDefined()) {
                throw std::runtime_error("'actor_module' must be set");
            }
            if (!actor_detail["actor_service"].IsDefined()) {
                throw std::runtime_error("'actor_service' must be set");
            }

            actor_config cfg;
            cfg.name = actor_detail["actor_name"].as<std::string>();
            cfg.count = actor_detail["actor_count"].as<uint32_t>();
            cfg.lib = actor_detail["actor_module"].as<std::string>();
            if (actor_detail["actor_address"].IsDefined() &&
                actor_detail["actor_port"].IsDefined()) {
                cfg.address = actor_detail["actor_address"].as<std::string>();
                cfg.port = actor_detail["actor_port"].as<uint16_t>();
            }

            if (check_actor_name.contains(cfg.name)) {
                throw std::runtime_error(fmt::format(
                    "Cannot set the same 'actor_name' : [{}]", cfg.name));
            } else {
                check_actor_name.emplace(cfg.name);
            }

            YAML::Node services =
                YAML::LoadFile(actor_detail["actor_service"].as<std::string>());
            for (const auto& service : services["services"]) {
                auto service_name = service.as<std::string>();
                if (check_service.contains(service_name)) {
                    throw std::runtime_error(
                        fmt::format("Cannot set the same 'service_name' : [{}]",
                                    service_name));
                } else {
                    check_service.emplace(service_name);
                }

                this->service_map_[service_name] = cfg.name;

                cfg.services.emplace(std::move(service_name));
            }

            this->actor_config_map_.emplace(cfg.name, std::move(cfg));
        }

    } catch (const std::exception& e) {
        SPDLOG_ERROR("failed to parse config file: [{}], error info: {}", file,
                     e.what());
        return false;
    }

    return true;
}
