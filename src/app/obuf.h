#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * obuf：简单环形缓冲区
 *
 * 目的：把“字节流输入”（例如串口中断里收到的每个字节）先存起来，
 * 然后在主循环/任务里按协议去找帧头、判断长度、校验、再一次性取出完整帧。
 *
 * 这样可以解决：串口数据可能被分多次到达、主循环解析不及时导致丢包等问题。
 */

typedef struct {
    uint8_t *buf;   /* 缓冲区指针 */
    size_t capacity;/* 总容量 */
    volatile size_t head; /* 写指针 (Write Index, Producer owns this) */
    volatile size_t tail; /* 读指针 (Read Index, Consumer owns this) */
    volatile size_t dropped; /* 丢弃计数 */
} obuf_t;

void obuf_init(obuf_t *o, uint8_t *storage, size_t capacity); /* 初始化 */
void obuf_clear(obuf_t *o);                                   /* 清空 */

/* 获取当前缓冲区内可读数据长度 */
size_t obuf_data_len(const obuf_t *o);

/* 写入字节（若写入超出容量，会丢弃最早的数据以保持最新数据） */
void obuf_write(obuf_t *o, const uint8_t *data, size_t n);     /* 写入（覆盖最旧数据） */

/* 读取并消费字节 */
size_t obuf_read(obuf_t *o, uint8_t *out, size_t n);           /* 读取并消费 */

/* 查看（不消费）从头开始第 index 个字节 */
int obuf_peek(const obuf_t *o, size_t index);                  /* 仅查看（不消费） */

/* 删除（消费）前 n 个字节 */
void obuf_drop(obuf_t *o, size_t n);                           /* 丢弃前 n 字节 */

/* 在缓冲区中查找 pattern（返回偏移；未找到返回 -1） */
int obuf_find(const obuf_t *o, const uint8_t *pattern, size_t pattern_len); /* 查找帧头 */

#ifdef __cplusplus
}
#endif
