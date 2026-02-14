#include "obuf.h"
#include <string.h>

/*
 * obuf - 单生产者/单消费者(SPSC)环形缓冲
 *
 * 线程模型:
 * - 生产者：串口ISR，仅修改 head，仅读取 tail
 * - 消费者：主循环，仅修改 tail，仅读取 head
 *
 * 设计要点:
 * 1) ISR 侧写入必须无阻塞、低开销
 * 2) 缓冲满时“丢新字节”，避免 ISR 与主循环同时改 tail 造成竞态
 * 3) 通过 dropped 统计丢字节数量，便于排查丢包
 */

void obuf_init(obuf_t *o, uint8_t *storage, size_t capacity)
{
    o->buf = storage;
    o->capacity = capacity;
    o->head = 0;
    o->tail = 0;
    o->dropped = 0;
}

void obuf_clear(obuf_t *o)
{
    /* Not strictly thread-safe if ISR is active, but works for reset */
    o->head = 0;
    o->tail = 0;
    o->dropped = 0;
}

/* 计算当前可读数据长度
 * - 只做简单算术，不做互斥
 * - 依赖 SPSC 模型保证 head/tail 不被同一线程同时修改
 */
size_t obuf_data_len(const obuf_t *o)
{
    size_t h = o->head;
    size_t t = o->tail;
    if (h >= t) return h - t;
    return h + o->capacity - t;
}

void obuf_write(obuf_t *o, const uint8_t *data, size_t n)
{
    /* ISR 侧写入：逐字节入队，满则丢新数据 */
    for (size_t i = 0; i < n; i++) {
        size_t next_head = (o->head + 1) % o->capacity;

        /* Buffer full: drop NEW incoming byte (safe for SPSC) */
        if (next_head == o->tail) {
            o->dropped++;
            continue;
        }

        o->buf[o->head] = data[i];
        o->head = next_head;
    }
}

size_t obuf_read(obuf_t *o, uint8_t *out, size_t n)
{
    size_t i = 0;
    size_t t = o->tail;
    size_t h = o->head;
    
    /* 简化实现：按字节读取，逻辑清晰；足以满足串口速率 */
    for (i = 0; i < n; i++) {
        if (t == h) { // Empty
            break;
        }
        out[i] = o->buf[t];
        t = (t + 1) % o->capacity;
    }
    
    o->tail = t; /* 统一更新尾指针，减少共享变量写入次数 */
    return i;
}

/*
 * obuf_peek: 读取“从队头起第 index 个字节”
 * - 不消费数据，用于解析时查看帧头/长度/校验
 */
int obuf_peek(const obuf_t *o, size_t index)
{
    size_t len = obuf_data_len(o);
    if (index >= len) {
        return -1;
    }

    /* tail + index wrapped */
    size_t pos = (o->tail + index) % o->capacity;
    return (int)o->buf[pos];
}

void obuf_drop(obuf_t *o, size_t n)
{
    size_t len = obuf_data_len(o);
    if (n > len) {
        n = len; /* Limit to available */
    }
    
    /* 推进尾指针，相当于丢弃 n 字节 */
    o->tail = (o->tail + n) % o->capacity;
}

int obuf_find(const obuf_t *o, const uint8_t *pattern, size_t pattern_len)
{
    size_t len = obuf_data_len(o);
    
    if (pattern_len == 0 || pattern_len > len) {
        return -1;
    }

    /* 暴力搜索帧头：足够快且实现简单 */
    for (size_t i = 0; i <= len - pattern_len; i++) {
        size_t matched = 0;
        for (size_t j = 0; j < pattern_len; j++) {
            // Manual peek
            size_t pos = (o->tail + i + j) % o->capacity;
            if (o->buf[pos] != pattern[j]) {
                break;
            }
            matched++;
        }
        
        if (matched == pattern_len) {
            return (int)i;
        }
    }

    return -1;
}
