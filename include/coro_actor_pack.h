#pragma once

#include "coro_actor_common.h"

/* 内置经典的 TLV 报文格式 */
struct coro_actor_pack_t {
    uint8_t     type;   /* 数据类型, 用来表示 value 中的数据格式 */
    std::string value;  /* 数据实体, 可以看做是一个二进制数据, 可以存放明文或序列化之后的字节流 */
};