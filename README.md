## coro_actor 简介

一个基于 Asio C++20 协程开发的简易 Actor 框架, 采用多进程 + 协程的并发模型, 进程间通信使用 Unix 域套接字, 实现底层调度完全无锁, 仅支持 Linux 平台

## coro_actor 工作原理

使用 Actor 框架的目的是为了简化并发, 每个 Actor 节点可以看做一个独立的计算单元, 不同的 Actor 节点之间通过消息进行通信, 一般在实现上为了更好的做到这一点, 一些框架如 `skynet` 通过引入 `Lua` 进行 Actor 运行环境隔离, 通过 `Lua API` 和底层的网络调度逻辑结合, 这样性能也能得到保障, 并且使用 `Lua` 脚本语言开发业务的效率非常高, 可以避免 C/C++ 语言编译时间长的问题

coro_actor 希望用户能直接使用 C++ 进行业务开发, 因此为了做到运行环境隔离, 使用一个操作系统进程来挂载 Actor, 每个 Actor 进程都加载它们各自的动态库, 每个动态库中可以提供多个服务(满足指定函数签名的 C 函数), 框架内部使用 `dlopen` 进行动态库运行时加载, 用户实现具体服务之后在配置文件中设置动态库路径以及服务名等信息后即可将服务加载到框架中, 这样的好处是当服务逻辑变动时, 只需要重新编译对应的动态库即可, 缓解了 C/C++ 语言编译时间长的问题

对于每个 Actor 进程, 框架保证服务都是顺序执行的, 每个服务都运行在一个协程中, 服务中可以通过导出的全局上下文 `g_actor_ctx` 获取 Actor 进程底层使用的 Asio io_context, 通过这个上下文, 我们可以在服务需要进行网络通信时使用协程进行处理, 避免使用阻塞网络 io 而影响整个 Actor进程的调度, 当服务执行完后, 用户需要显式通知上下文处理完成, 因为在服务处理完成之前, 框架内部的协程是处于等待状态的

通过全局上下文我们可以在一个 Actor 进程的服务中调用另一个 Actor 进程提供的服务, 在框架底层通过引入一个 Proxy 代理进程进行请求转发和负载均衡, 它们之间通过 Unix 域套接字进行通信, 可以很好地融入 Asio 协程上下文, 并且由协程保证了不会出现并发资源访问的问题, 做到完全无锁, 由于 Proxy 进程只进行消息转发, 不需要处理具体业务逻辑, 故只使用一个进程即可, 不会有太大的性能问题

## 构建

```bash
git clone --recurse-submodules https://github.com/xukeawsl/coro_actor.git
cd coro_actor
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

## 运行

编写完配置文件后通过命令启动即可

```bash
# 查看帮助
./coro_actor -h

# 通过配置文件启动 coro_actor
./coro_actor -c ../actor_config.yml

# 通过配置文件关闭 coro_actor
./coro_actor -s ../actor_config.yml
```

## 安装

```bash
# 安装到默认路径 /usr/local 下的 include、bin 和 lib
cmake --install .

# 安装到指定前缀路径
cmake --install . --prefix /usr/local/coro_actor
```