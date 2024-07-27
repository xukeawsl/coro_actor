#pragma once

#include <queue>
#include <list>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>

#include <dlfcn.h>
#include <unistd.h>
#include <sys/wait.h>

#include "asio.hpp"
#include "asio/experimental/channel.hpp"

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"

#include "ylt/struct_pack.hpp"

#include "cxxopts.hpp"