/**
 ****************************************************************************************************
 * @file        main.c
 * @author      正点原子团队(ALIENTEK) + 工业看板移植
 * @version     V2.0
 * @date        2026-01-23
 * @brief       STM32F767 工业数据显示系统 - 基于LVGL + SQMWD_Tablet协议
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * 【模块说明 / Module Description】
 * 模块名称: Main Entry (主程序入口模块)
 * 文件路径: d:\LVGL\LVGL1\User\main.c
 * 
 * 功能概述:
 * 1. 系统初始化: 负责时钟(216MHz), 串口(UART1), SDRAM, LCD(LTDC), 定时器等底层外设的初始化。
 * 2. 协议解析: 实现了 "SQMWD_Tablet" 协议的串口数据流解析。
 *    - 协议格式: Header(2B) + CMD(1B) + LEN(1B) + Sub_CMD(1B) + Payload + XOR(1B)
 * 3. 数据分发: 将解析出的物理量 (井斜/方位/工具面等) 填充到业务结构体 (g_metrics)，
 *    并调用 UI层的 dashboard_update() 进行显示刷新。
 * 4. 主循环: 驱动 LVGL 的任务调度 (lv_timer_handler) 和 串口数据轮询。
 ****************************************************************************************************
 */

/* 用户应用层头文件 */
#include "app/app.h"          /* 应用层主入口声明 (app_init) */
#include "app/obuf.h"         /* 环形缓冲区工具库 (Ring Buffer) */
#include "app/screens/dashboard.h" /* 仪表盘UI更新接口 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* 正点原子BSP驱动 (Board Support Package) */
#include "./SYSTEM/sys/sys.h"       /* 系统时钟配置 */
#include "./SYSTEM/usart/usart.h"   /* 串口驱动 */
#include "./SYSTEM/delay/delay.h"   /* 延时函数 */
#include "./BSP/LED/led.h"          /* LED控制 */
#include "./BSP/LCD/lcd.h"          /* LCD底层驱动 */
#include "./BSP/SDRAM/sdram.h"      /* 外部SDRAM驱动 (显存) */
#include "./BSP/MPU/mpu.h"          /* 内存保护单元配置 */
#include "./BSP/TIMER/btim.h"       /* 基本定时器 (提供1ms心跳) */

/* LVGL 图形库 */
#include "lvgl.h"
#include "lv_port_disp_template.h"  /* LVGL 显示接口适配 */
 

/* =============== 全局变量定义 =============== */

/* 串口接收底层 buffer (8KB)
 * 说明：越大越不容易在高峰期被填满，但也会占用更多 RAM
 */
static uint8_t g_rx_storage[8192];    

/* 环形缓冲区管理结构体 (在串口中断和主循环间共享数据) */
obuf_t g_rx_buf;                      

/* 串口稳定性控制 */
static volatile uint32_t g_uart_ignore_until_ms = 0; /* 上电静默截止时间 */
static uint32_t g_last_frame_ms = 0;                 /* 最近一次有效帧时间 */
static volatile uint32_t g_last_rx_byte_ms = 0;       /* 最近一次收到字节时间 */

/* 解析超时统计 */
static uint32_t g_parse_timeout_cnt = 0;

/*
 * 宏开关: 是否启用 SQMWD_Tablet 协议解析
 * 当前阶段先跑通“真实 SQMWD_Tablet 数据”，设为 1。
 */
#define APP_ENABLE_TABLET_PARSE 1

#if APP_ENABLE_TABLET_PARSE
/* 业务数据存储对象 (各模块共享的单一数据源) */
static plant_metrics_t g_metrics;     
/* 标志位: 收到真实数据后置位 (虽然目前未广泛使用，可用于切换UI模式) */
static uint8_t g_has_real_data = 0;

/* 调试信息 */
static dashboard_debug_info_t g_dbg_info;

static plant_metrics_t g_last_metrics;
static uint8_t g_last_metrics_valid = 0;
static dashboard_debug_info_t g_last_dbg_info;
static uint8_t g_last_dbg_valid = 0;
static uint32_t g_last_dbg_tick = 0;
static uint32_t g_last_decode_tick = 0;
/* 解码表刷新缓存：仅保留最近一条 + 时间戳 */
static char g_decode_name[32];
static float g_decode_value = 0.0f;
static uint8_t g_decode_highlight = 0;
static uint32_t g_decode_last_ms = 0;
#endif

/*
 * ============================================================================
 * 函数: usart_rx_byte_hook
 * 功能: 串口接收字节钩子函数
 * 说明: 此函数在 Drivers/SYSTEM/usart/usart.c 的接收中断服务程序(ISR)中被调用。
 *       每当 UART1 接收到一个字节，就会调用此函数。
 * 
 * 参数: byte - 接收到的单个字节
 *
 * 逻辑:
 * 1. 将接收到的字节直接压入环形缓冲区 (g_rx_buf)。
 * 2. 只有在此处快速缓存，才能应对 115200 波特率下的连续数据流，避免丢包。
 * 3. 提供一个 LED 翻转作为物理层的“心跳”指示 (每50字节翻转一次)，方便肉眼判断是否有数据进来。
 * ============================================================================
 */
void usart_rx_byte_hook(uint8_t byte)
{
    /* 上电/打开串口静默期：丢弃毛刺字节 */
    if (HAL_GetTick() < g_uart_ignore_until_ms) {
        return;
    }

    /* 1. 压入环形缓冲，供主循环消费 */
    obuf_write(&g_rx_buf, &byte, 1);

    g_last_rx_byte_ms = HAL_GetTick();

    /* 调试：统计接收字节数 */
    g_dbg_info.rx_bytes++;

    /* 2. 快速连通性验证 (LED Flash) */
    static uint16_t rx_cnt = 0;
    if (++rx_cnt >= 50) {
        rx_cnt = 0;
        LED0_TOGGLE(); // 翻转 LED0 (红色)
    }
}

#if APP_ENABLE_TABLET_PARSE
typedef struct {
    /* 仅保留新协议 CMD=0x09 */
    uint8_t cmd;

    uint8_t sub_cmd;
    uint8_t fid;
    float f1;
    float f2;
    float auto_close_sec;
    char text[128];
    uint8_t has_fid;
    uint8_t has_f2;
    uint8_t has_text;
} sx_frame_t;

static void ascii_lower_copy(const char *src, char *dst, size_t cap)
{
    if (!src || !dst || cap == 0) {
        return;
    }
    size_t i = 0;
    for (; i + 1 < cap && src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 0x80) {
            dst[i] = (char)tolower(c);
        } else {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

/* 不区分大小写的子串匹配（英文关键词） */
static int str_contains_ci(const char *s, const char *needle)
{
    if (!s || !needle) {
        return 0;
    }
    char s_low[128];
    char n_low[64];
    ascii_lower_copy(s, s_low, sizeof(s_low));
    ascii_lower_copy(needle, n_low, sizeof(n_low));
    return strstr(s_low, n_low) != NULL;
}

/* ASCII 字符是否是字母/数字/下划线 (用于英文缩写的词边界判断) */
static int is_ascii_alnum(char c)
{
    if (c >= '0' && c <= '9') return 1;
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c == '_') return 1;
    return 0;
}

/* 英文 token 匹配（可选词边界） */
/* 英文缩写匹配：默认启用单词边界，避免误匹配 */
static int contains_token_ci(const char *s, const char *token, int word_boundary)
{
    if (!s || !token) return 0;
    char s_low[160];
    char t_low[64];
    ascii_lower_copy(s, s_low, sizeof(s_low));
    ascii_lower_copy(token, t_low, sizeof(t_low));

    const char *p = s_low;
    size_t tlen = strlen(t_low);
    if (tlen == 0) return 0;

    while ((p = strstr(p, t_low)) != NULL) {
        if (!word_boundary) {
            return 1;
        }
        char prev = (p == s_low) ? '\0' : *(p - 1);
        char next = *(p + tlen);
        if (!is_ascii_alnum(prev) && !is_ascii_alnum(next)) {
            return 1;
        }
        p += tlen;
    }
    return 0;
}

/* 英文短语匹配：同时支持带空格/下划线和去空格的匹配 */
/* 英文短语匹配：支持空格/下划线/连写 */
static int contains_phrase_ci(const char *s, const char *phrase)
{
    if (!s || !phrase) return 0;

    if (str_contains_ci(s, phrase)) {
        return 1;
    }

    /* 归一化：去掉空格与下划线，便于匹配 "gravitytoolface" 这种写法 */
    char s_norm[160];
    char p_norm[80];
    size_t si = 0;
    for (size_t i = 0; s[i] != '\0' && si + 1 < sizeof(s_norm); i++) {
        char c = s[i];
        if (c == ' ' || c == '_') continue;
        if ((unsigned char)c < 0x80) {
            s_norm[si++] = (char)tolower((unsigned char)c);
        } else {
            s_norm[si++] = c;
        }
    }
    s_norm[si] = '\0';

    size_t pi = 0;
    for (size_t i = 0; phrase[i] != '\0' && pi + 1 < sizeof(p_norm); i++) {
        char c = phrase[i];
        if (c == ' ' || c == '_') continue;
        if ((unsigned char)c < 0x80) {
            p_norm[pi++] = (char)tolower((unsigned char)c);
        } else {
            p_norm[pi++] = c;
        }
    }
    p_norm[pi] = '\0';

    return strstr(s_norm, p_norm) != NULL;
}

/* 字段识别配置表：与 SQMWD_Tablet 正则规则保持一致，便于扩展新别名 */
typedef enum {
    FIELD_NONE = 0,
    FIELD_SYNC,
    FIELD_INC,
    FIELD_AZI,
    FIELD_GTF,
    FIELD_MTF,
    FIELD_TF
} field_kind_t;

/* SQMWD_Tablet 旧协议中的 FID 编码（用于 0x09/0x02 子命令） */
typedef enum {
    OLD_DT_NONE = 0x00,        /* 同步头 */
    OLD_DT_INC  = 0x10,        /* 井斜 */
    OLD_DT_AZI  = 0x11,        /* 方位 */
    OLD_DT_TF   = 0x12,        /* 工具面 */
    OLD_DT_GTF  = 0x13,        /* 重力工具面 */
    OLD_DT_MTF  = 0x14         /* 磁性工具面 */
} probe_data_type_t;

typedef struct {
    field_kind_t kind;
    uint8_t highlight;                 /* 是否高亮（同步头） */
    const char *desc;                  /* 备注说明，便于维护 */
    const char * const *cn_keywords;   /* 中文关键字 */
    const char * const *phrases;       /* 英文短语（支持空格/下划线/连写） */
    const char * const *tokens;        /* 英文缩写（单词边界） */
} field_rule_t;

static const char *const k_sync_cn[] = {"同步头", "同步", NULL};
static const char *const k_sync_tokens[] = {"fid", "sync", NULL};

static const char *const k_inc_cn[] = {"井斜角", "井斜", "倾角", NULL};
static const char *const k_inc_phrases[] = {"inclination", "deviation", "static_inc", "continue_inc", NULL};
static const char *const k_inc_tokens[] = {"inc", NULL};

static const char *const k_azi_cn[] = {"方位角", "方位", NULL};
static const char *const k_azi_phrases[] = {"azimuth angle", "azimuth", "static_azi", "continue_azi", NULL};
static const char *const k_azi_tokens[] = {"azi", NULL};

static const char *const k_gtf_cn[] = {"重力工具面", "重力高边角", "重力高边", NULL};
static const char *const k_gtf_phrases[] = {"gravity tool face", "gravity high side angle", "gravity high side", NULL};
static const char *const k_gtf_tokens[] = {"gtf", "ghsa", "ghs", NULL};

static const char *const k_mtf_cn[] = {"磁性工具面", "磁工具面", "磁性高边角", "磁高边角", "磁性高边", "磁高边", NULL};
static const char *const k_mtf_phrases[] = {"magnetic tool face", "magnetic high side angle", "magnetic high side", NULL};
static const char *const k_mtf_tokens[] = {"mtf", "mhsa", "mhs", NULL};

static const char *const k_tf_cn[] = {"工具面", NULL};
static const char *const k_tf_phrases[] = {"toolface", "tool face", NULL};

static const field_rule_t k_field_rules[] = {
    {FIELD_SYNC, 1, "sync", k_sync_cn, NULL, k_sync_tokens},
    {FIELD_GTF,  0, "gtf",  k_gtf_cn,  k_gtf_phrases, k_gtf_tokens},
    {FIELD_MTF,  0, "mtf",  k_mtf_cn,  k_mtf_phrases, k_mtf_tokens},
    {FIELD_TF,   0, "tf",   k_tf_cn,   k_tf_phrases,  NULL},
    {FIELD_INC,  0, "inc",  k_inc_cn,  k_inc_phrases, k_inc_tokens},
    {FIELD_AZI,  0, "azi",  k_azi_cn,  k_azi_phrases, k_azi_tokens},
};

/* 中文关键词匹配 */
static int contains_any_cn(const char *s, const char * const *list)
{
    if (!s || !list) return 0;
    for (int i = 0; list[i] != NULL; i++) {
        if (strstr(s, list[i]) != NULL) return 1;
    }
    return 0;
}

/* 英文短语匹配 */
static int contains_any_phrase(const char *s, const char * const *list)
{
    if (!s || !list) return 0;
    for (int i = 0; list[i] != NULL; i++) {
        if (contains_phrase_ci(s, list[i])) return 1;
    }
    return 0;
}

/* 英文缩写匹配（单词边界） */
static int contains_any_token(const char *s, const char * const *list)
{
    if (!s || !list) return 0;
    for (int i = 0; list[i] != NULL; i++) {
        if (contains_token_ci(s, list[i], 1)) return 1;
    }
    return 0;
}

static int debug_info_changed(const dashboard_debug_info_t *cur, const dashboard_debug_info_t *last)
{
    if (!cur || !last) {
        /* 任意指针为空：认为需要刷新 */
        return 1;
    }

    /* 任意关键字段变化都触发刷新，避免无效刷屏 */
    if (cur->rx_bytes != last->rx_bytes) return 1;
    if (cur->rx_isr != last->rx_isr) return 1;
    if (cur->try_cnt != last->try_cnt) return 1;
    if (cur->frames_ok != last->frames_ok) return 1;
    if (cur->frames_bad != last->frames_bad) return 1;
    if (cur->rx_overflow != last->rx_overflow) return 1;
    if (cur->parse_timeout != last->parse_timeout) return 1;
    if (cur->drop_no_header != last->drop_no_header) return 1;
    if (cur->drop_len != last->drop_len) return 1;
    if (cur->drop_cmd != last->drop_cmd) return 1;
    if (cur->drop_chk != last->drop_chk) return 1;
    if (cur->err_ore != last->err_ore) return 1;
    if (cur->err_fe != last->err_fe) return 1;
    if (cur->err_ne != last->err_ne) return 1;
    if (cur->err_pe != last->err_pe) return 1;
    if (cur->last_len != last->last_len) return 1;
    if (cur->last_chk != last->last_chk) return 1;
    if (cur->last_calc != last->last_calc) return 1;
    if (cur->last_err != last->last_err) return 1;
    if (cur->last_sub_cmd != last->last_sub_cmd) return 1;
    if (fabsf(cur->last_value - last->last_value) > 0.001f) return 1;
    if (strncmp(cur->last_name, last->last_name, sizeof(cur->last_name)) != 0) return 1;
    if (strncmp(cur->last_raw, last->last_raw, sizeof(cur->last_raw)) != 0) return 1;

    /* 所有字段都一致：无需刷新 */
    return 0;
}

/* 生成调试用原始HEX摘要（最多16字节）
 * 用途：当无法定位帧头/校验异常时，直接看到输入流原始字节
 */
static void dbg_fill_raw_hex(obuf_t *in, size_t max_bytes, char *out, size_t cap)
{
    if (!in || !out || cap == 0) return;
    out[0] = '\0';
    size_t len = obuf_data_len(in);
    if (max_bytes > len) max_bytes = len;
    if (max_bytes > 16) max_bytes = 16;
    size_t pos = 0;
    pos += snprintf(out + pos, cap - pos, "RAW:");
    for (size_t i = 0; i < max_bytes && pos + 4 < cap; i++) {
        int b = obuf_peek(in, i);
        if (b < 0) break;
        pos += snprintf(out + pos, cap - pos, " %02X", (unsigned)b);
    }
}

typedef struct {
    field_kind_t kind;
    uint8_t highlight;
} field_match_t;

static field_match_t match_field_name(const char *name)
{
    field_match_t m = {FIELD_NONE, 0};
    if (!name) return m;

    /* 仅用于“显示名称”的兜底匹配，不参与FID主逻辑 */
    for (size_t i = 0; i < (sizeof(k_field_rules) / sizeof(k_field_rules[0])); i++) {
        const field_rule_t *r = &k_field_rules[i];
        if (contains_any_cn(name, r->cn_keywords) ||
            contains_any_phrase(name, r->phrases) ||
            contains_any_token(name, r->tokens)) {
            m.kind = r->kind;
            m.highlight = r->highlight;
            return m;
        }
    }
    /* 未匹配：保持 FIELD_NONE */
    return m;
}

/* 按FID直接映射字段（优先使用，避免复杂字符串匹配） */
static int match_field_by_fid(uint8_t fid, field_match_t *m, const char **name_cn)
{
    if (!m || !name_cn) {
        return 0;
    }

    *name_cn = NULL;
    m->kind = FIELD_NONE;
    m->highlight = 0;

    switch (fid) {
    case OLD_DT_NONE:
        m->kind = FIELD_SYNC;
        m->highlight = 1;
        *name_cn = "同步头";
        return 1;
    case OLD_DT_INC:
        m->kind = FIELD_INC;
        *name_cn = "井斜";
        return 1;
    case OLD_DT_AZI:
        m->kind = FIELD_AZI;
        *name_cn = "方位";
        return 1;
    case OLD_DT_TF:
        m->kind = FIELD_TF;
        *name_cn = "工具面";
        return 1;
    case OLD_DT_GTF:
        m->kind = FIELD_GTF;
        *name_cn = "重力工具面";
        return 1;
    case OLD_DT_MTF:
        m->kind = FIELD_MTF;
        *name_cn = "磁性工具面";
        return 1;
    default:
        break;
    }

    return 0;
}

static const char *field_display_name(field_kind_t kind, const char *raw, char *buf, size_t cap)
{
    if (!buf || cap == 0) {
        return raw ? raw : "";
    }

    switch (kind) {
    case FIELD_INC:
        snprintf(buf, cap, "井斜 Inc");
        return buf;
    case FIELD_AZI:
        snprintf(buf, cap, "方位 Azi");
        return buf;
    case FIELD_GTF:
        snprintf(buf, cap, "重力高边角 GTF");
        return buf;
    case FIELD_MTF:
        snprintf(buf, cap, "磁性高边角 MTF");
        return buf;
    case FIELD_TF:
        snprintf(buf, cap, "工具面 TF");
        return buf;
    case FIELD_SYNC:
        snprintf(buf, cap, "同步头 Sync");
        return buf;
    default:
        break;
    }

    if (raw && raw[0] != '\0') {
        return raw;
    }
    return "";
}

/*
 * ============================================================================
 * 函数: sx_try_parse_one
 * 功能: 解析新版协议帧 (40 46 09)
 * 帧格式:
 * [0-1] Header: 0x40 0x46
 * [2]   CMD:    0x09
 * [3]   LEN:    Payload长度
 * [4]   Sub_CMD
 *    Sub_CMD=0x01: [f1(4B)][f2(4B)]
 *    Sub_CMD=0x02: [FID(1B)][f1(4B)][Name...]
 *    Sub_CMD=0x03: [FID(1B)][autoCloseSec(4B)][Message...]
 * [end] XOR Checksum (对前面所有字节异或)
 * ============================================================================
 */
/* 解析一帧：成功返回1，失败/不完整返回0
 * 解析流程：找帧头 -> 校验长度 -> 校验CMD -> 校验XOR -> 解包
 */
static int sx_try_parse_one(obuf_t *in, sx_frame_t *out)
{
    const uint8_t header[2] = {0x40, 0x46};

    /* 低速数据链路：减少防护循环次数，降低CPU占用 */
    for (int guard = 0; guard < 8; guard++) {
        int off = obuf_find(in, header, sizeof(header));
        /* 1) 缓冲区内找不到帧头：丢弃旧数据，仅保留末尾1字节 */
        if (off < 0) {
            size_t len = obuf_data_len(in);
            if (len > 1) {
                obuf_drop(in, len - 1);
            }
            g_dbg_info.drop_no_header++;
            g_dbg_info.last_err = 1;
            g_dbg_info.last_len = 0;
            g_dbg_info.last_chk = 0;
            g_dbg_info.last_calc = 0;
            dbg_fill_raw_hex(in, 16, g_dbg_info.last_raw, sizeof(g_dbg_info.last_raw));
            return 0;
        }

        /* 2) 若帧头前有噪声，直接丢弃 */
        if (off > 0) {
            obuf_drop(in, (size_t)off);
        }

        /* 3) 长度不足一帧最小值(5B)则等待 */
        if (obuf_data_len(in) < 5) {
            return 0;
        }

        int cmd = obuf_peek(in, 2);
        int len = obuf_peek(in, 3);
        if (cmd < 0 || len < 0) {
            return 0;
        }

        /* 4) LEN 合法性保护：防止异常长度导致解析卡死 */
        if ((uint8_t)len == 0 || (uint8_t)len > 200) {
            g_dbg_info.frames_bad++;
            g_dbg_info.drop_len++;
            g_dbg_info.last_err = 1;
            g_dbg_info.last_len = (uint8_t)len;
            g_dbg_info.last_chk = 0;
            g_dbg_info.last_calc = 0;
            dbg_fill_raw_hex(in, 8, g_dbg_info.last_raw, sizeof(g_dbg_info.last_raw));
            obuf_drop(in, 1);
            continue;
        }

        if ((uint8_t)cmd != 0x09) {
            /* 5) 只保留新协议 0x09：丢 1 字节继续找头 */
            g_dbg_info.drop_cmd++;
            obuf_drop(in, 1);
            continue;
        }

        size_t frame_len = (size_t)((uint8_t)len) + 5;
        /* 6) 数据未到齐：等待后续字节 */
        if (obuf_data_len(in) < frame_len) {
            return 0;
        }

        uint8_t calc = 0;
        for (size_t i = 0; i < frame_len - 1; i++) {
            int b = obuf_peek(in, i);
            if (b < 0) return 0;
            calc ^= (uint8_t)b;
        }
        int chk = obuf_peek(in, frame_len - 1);
        if (chk < 0) return 0;
        if ((uint8_t)chk != calc) {
            /* 7) XOR 校验失败：丢弃1字节继续找头 */
            g_dbg_info.frames_bad++;
            g_dbg_info.drop_chk++;
            g_dbg_info.last_err = 1;
            g_dbg_info.last_len = (uint8_t)len;
            g_dbg_info.last_chk = (uint8_t)chk;
            g_dbg_info.last_calc = calc;
            dbg_fill_raw_hex(in, frame_len, g_dbg_info.last_raw, sizeof(g_dbg_info.last_raw));
            obuf_drop(in, 1);
            continue;
        }

        g_dbg_info.last_err = 0;
        g_dbg_info.last_len = (uint8_t)len;
        g_dbg_info.last_chk = (uint8_t)chk;
        g_dbg_info.last_calc = calc;
        g_dbg_info.last_raw[0] = '\0';

        memset(out, 0, sizeof(*out));
        out->cmd = (uint8_t)cmd;

        out->sub_cmd = (uint8_t)obuf_peek(in, 4);

        /* 8) 根据子命令解析 Payload */
        if (out->sub_cmd == 0x01 && (uint8_t)len >= 9) {
            uint8_t fraw[4];
            for (int i = 0; i < 4; i++) {
                fraw[i] = (uint8_t)obuf_peek(in, 5 + i);
            }
            memcpy(&out->f1, fraw, sizeof(float));
            for (int i = 0; i < 4; i++) {
                fraw[i] = (uint8_t)obuf_peek(in, 9 + i);
            }
            memcpy(&out->f2, fraw, sizeof(float));
            out->has_f2 = 1;
        } else if ((out->sub_cmd == 0x02 || out->sub_cmd == 0x03) && (uint8_t)len >= 6) {
            out->fid = (uint8_t)obuf_peek(in, 5);
            out->has_fid = 1;

            uint8_t fraw[4];
            for (int i = 0; i < 4; i++) {
                fraw[i] = (uint8_t)obuf_peek(in, 6 + i);
            }
            memcpy(&out->f1, fraw, sizeof(float));
            if (out->sub_cmd == 0x03) {
                out->auto_close_sec = out->f1;
            }

            int text_len = (int)((uint8_t)len) - 6;
            if (text_len > 0) {
                int cap = (int)sizeof(out->text) - 1;
                if (text_len > cap) text_len = cap;
                for (int i = 0; i < text_len; i++) {
                    int b = obuf_peek(in, 10 + (size_t)i);
                    out->text[i] = (b < 0) ? '\0' : (char)b;
                }
                out->text[text_len] = '\0';
                out->has_text = 1;
            }
        }

        /* 9) 消费整帧数据 */
        obuf_drop(in, frame_len);
        /* 调试：成功解析帧计数 */
        g_dbg_info.frames_ok++;
            g_last_frame_ms = HAL_GetTick();
        return 1;
    }
    /* 防御性退出：避免在极端情况下卡在解析循环 */
    obuf_drop(in, 1);
    return 0;
}
#endif

int main(void)
{
    /* ========== 1. 硬件底层初始化 ========== */
    sys_cache_enable();                         /* 打开L1-Cache (提升性能) */
    HAL_Init();                                 /* 初始化HAL库 */
    sys_stm32_clock_init(432, 25, 2, 9);        /* 配置系统时钟: 216MHz */
    delay_init(216);                            /* 初始化延时函数 */

    /* 初始化协议接收环形缓冲区（必须在串口接收中断开始写入前完成） */
    obuf_init(&g_rx_buf, g_rx_storage, sizeof(g_rx_storage));

    usart_init(UART_DEFAULT_BAUDRATE);          /* 初始化串口 (接收电脑数据) */
    g_uart_ignore_until_ms = HAL_GetTick() + 300; /* 300ms 静默期过滤串口毛刺 */
    led_init();                                 /* 初始化LED指示灯 */
    mpu_memory_protection();                    /* 配置MPU保护 */
    sdram_init();                               /* 初始化SDRAM (显存) */
    lcd_init();                                 /* 初始化LCD屏幕 *** 必须在lv_init前 *** */
    btim_timx_int_init(10-1, 10800-1);          /* 初始化定时器 (为LVGL提供1ms心跳) */
    
    /* ========== 2. LVGL图形库初始化 ========== */
    lv_init();                                  /* LVGL核心初始化 */
    lv_port_disp_init();                        /* 显示接口初始化 */
    
    /* ========== 3. 用户应用初始化 ========== */
    app_init(NULL);                             /* 创建工业看板UI（disp 参数预留，板端填 NULL） */
    
    /* ========== 4. 主循环 (无限) ========== */
    while(1)
    {
        g_dbg_info.try_cnt++;
        usart_rx_recover_if_needed();
        /* A. LVGL任务处理 (30Hz 刷新节流) */
        {
            static uint32_t last_lvgl_tick = 0;
            uint32_t now = lv_tick_get();
            if ((now - last_lvgl_tick) >= 100) {
                last_lvgl_tick = now;
                lv_timer_handler();
            }
        }

#if APP_ENABLE_TABLET_PARSE
        /* B. 串口数据解析与UI刷新（后续联调时启用） */
        /* 数据流: 串口中断 -> obuf_write -> g_rx_buf -> sx_try_parse_one -> 转换 -> dashboard_update */
        sx_frame_t frame;
        int process_cnt = 0; /* 本轮循环处理的数据包计数 */

        /* 
         * [优化]: 改为 while 循环，尽可能多地通过本轮循环消化缓冲区积压的数据。
         * 限制单次最大处理 50 包，防止数据量过大导致 UI 线程被饿死 (Watchdog 超时或界面卡顿)。
         */
        while (process_cnt < 50 && sx_try_parse_one(&g_rx_buf, &frame))
        {
            process_cnt++;

            /* 更新连接状态：只要收到有效帧就认为已连接 */
            g_metrics.port_connected = 1;
            strncpy(g_metrics.port_name, "UART2", sizeof(g_metrics.port_name) - 1);
            g_metrics.port_name[sizeof(g_metrics.port_name) - 1] = '\0';

            if (frame.cmd == 0x09 && frame.sub_cmd == 0x01 && frame.has_f2) {
                float press = (frame.f1 > 0.0f) ? frame.f1 : frame.f2;
                g_metrics.pump_pressure = press;
                g_metrics.pump_status = (press > 2.0f) ? 1 : 0;

                /* 调试信息更新 */
                g_dbg_info.last_sub_cmd = 0x01;
                strncpy(g_dbg_info.last_name, "泵压", sizeof(g_dbg_info.last_name) - 1);
                g_dbg_info.last_name[sizeof(g_dbg_info.last_name) - 1] = '\0';
                g_dbg_info.last_value = press;
            } else if (frame.cmd == 0x09 && frame.sub_cmd == 0x02) {
                field_match_t match = {FIELD_NONE, 0};
                const char *show_name = "";
                const char *fid_name = NULL;

                /* 解析主逻辑：仅按FID映射字段类型 */
                if (frame.has_fid && match_field_by_fid(frame.fid, &match, &fid_name)) {
                    show_name = fid_name;
                }

                /* 名称字符串仅用于显示兜底，不参与解析 */
                if (show_name[0] == '\0') {
                    show_name = frame.has_text ? frame.text : "";
                }

                /* 解析线程仅缓存“最近一条 + 时间戳”，避免堆积 */
                strncpy(g_decode_name, show_name, sizeof(g_decode_name) - 1);
                g_decode_name[sizeof(g_decode_name) - 1] = '\0';
                g_decode_value = frame.f1;
                g_decode_highlight = match.highlight ? 1 : 0;
                g_decode_last_ms = lv_tick_get();

                /* 调试信息更新 */
                g_dbg_info.last_sub_cmd = 0x02;
                strncpy(g_dbg_info.last_name, show_name, sizeof(g_dbg_info.last_name) - 1);
                g_dbg_info.last_name[sizeof(g_dbg_info.last_name) - 1] = '\0';
                g_dbg_info.last_value = frame.f1;

                /* 按字段类型写入业务数据 */
                if (match.kind == FIELD_INC) {
                    g_metrics.inclination = frame.f1;
                } else if (match.kind == FIELD_AZI) {
                    g_metrics.azimuth = frame.f1;
                } else if (match.kind == FIELD_GTF) {
                    g_metrics.toolface = frame.f1;
                    g_metrics.tf_type = 0x13;
                } else if (match.kind == FIELD_MTF) {
                    g_metrics.toolface = frame.f1;
                    g_metrics.tf_type = 0x14;
                } else if (match.kind == FIELD_TF) {
                    g_metrics.toolface = frame.f1;
                }

                if (match.kind == FIELD_TF || match.kind == FIELD_GTF || match.kind == FIELD_MTF) {
                    for (int i = 0; i < 4; i++) {
                        g_metrics.toolface_history[i] = g_metrics.toolface_history[i + 1];
                    }
                    g_metrics.toolface_history[4] = g_metrics.toolface;
                }
            } else if (frame.cmd == 0x09 && frame.sub_cmd == 0x03) {
                if (frame.has_text) {
                    uint32_t ms = 0;
                    if (frame.auto_close_sec > 0.0f) {
                        ms = (uint32_t)(frame.auto_close_sec * 1000.0f + 0.5f);
                    }
                    dashboard_show_message(frame.text, ms);
                }

                /* 调试信息更新 */
                g_dbg_info.last_sub_cmd = 0x03;
                strncpy(g_dbg_info.last_name, "消息", sizeof(g_dbg_info.last_name) - 1);
                g_dbg_info.last_name[sizeof(g_dbg_info.last_name) - 1] = '\0';
                g_dbg_info.last_value = frame.auto_close_sec;
            }

            if (!g_has_real_data) {
                g_has_real_data = 1;
                app_stop_sim();
            }

            /* 成功解析到一帧：翻转 LED1 用于确认协议解析成功 */
            LED1_TOGGLE();
        }

        /* 低频刷新解码表（与解析解耦，避免高频UI开销） */
        {
            uint32_t now = lv_tick_get();
            if ((now - g_last_decode_tick) >= 300 && g_decode_last_ms != 0) {
                dashboard_append_decode_row(g_decode_name, g_decode_value, g_decode_highlight);
                g_decode_last_ms = 0;
                g_last_decode_tick = now;
            }
        }

        /* 
         * [优化]: 批量处理完后再刷新 UI。
         * 如果本轮循环处理了至少 1 包数据，则更新显示。
         * 避免了每解一包就重绘一次 UI 的巨大开销。
         */
        if (process_cnt > 0) {
            g_dbg_info.rx_isr = g_uart_isr_cnt;
            g_dbg_info.err_ore = g_uart_err_ore;
            g_dbg_info.err_fe = g_uart_err_fe;
            g_dbg_info.err_ne = g_uart_err_ne;
            g_dbg_info.err_pe = g_uart_err_pe;
            g_dbg_info.rx_overflow = (uint32_t)g_rx_buf.dropped;
            g_dbg_info.buf_len = (uint32_t)obuf_data_len(&g_rx_buf);
            g_dbg_info.parse_timeout = g_parse_timeout_cnt;

            /* 仅当数据有变化时刷新主界面，降低CPU负担 */
            uint8_t need_update = 0;
            if (!g_last_metrics_valid) {
                need_update = 1;
            } else {
                const float eps = 0.01f;
                if (fabsf(g_metrics.inclination - g_last_metrics.inclination) > eps) need_update = 1;
                if (fabsf(g_metrics.azimuth - g_last_metrics.azimuth) > eps) need_update = 1;
                if (fabsf(g_metrics.toolface - g_last_metrics.toolface) > eps) need_update = 1;
                if (fabsf(g_metrics.pump_pressure - g_last_metrics.pump_pressure) > eps) need_update = 1;
                if (g_metrics.pump_status != g_last_metrics.pump_status) need_update = 1;
                if (g_metrics.port_connected != g_last_metrics.port_connected) need_update = 1;
                if (g_metrics.tf_type != g_last_metrics.tf_type) need_update = 1;
                if (strncmp(g_metrics.port_name, g_last_metrics.port_name, sizeof(g_metrics.port_name)) != 0) need_update = 1;
                for (int i = 0; i < 5; i++) {
                    if (fabsf(g_metrics.toolface_history[i] - g_last_metrics.toolface_history[i]) > eps) {
                        need_update = 1;
                        break;
                    }
                }
            }

            if (need_update) {
                dashboard_update(&g_metrics);
                g_last_metrics = g_metrics;
                g_last_metrics_valid = 1;
            }
            {
                uint32_t now = lv_tick_get();
                const uint32_t interval = 1000;
                uint8_t changed = (!g_last_dbg_valid) || debug_info_changed(&g_dbg_info, &g_last_dbg_info);
                if (changed && (now - g_last_dbg_tick) >= interval) {
                    dashboard_debug_update(&g_dbg_info);
                    g_last_dbg_info = g_dbg_info;
                    g_last_dbg_valid = 1;
                    g_last_dbg_tick = now;
                }
            }
        }

        /* 即使未解析到帧，也低频刷新调试面板（仅变化时刷新） */
        {
            static uint32_t last_dbg_tick = 0;
            uint32_t now = lv_tick_get();
            if ((now - last_dbg_tick) >= 1000) {
                last_dbg_tick = now;
                g_dbg_info.rx_isr = g_uart_isr_cnt;
                g_dbg_info.err_ore = g_uart_err_ore;
                g_dbg_info.err_fe = g_uart_err_fe;
                g_dbg_info.err_ne = g_uart_err_ne;
                g_dbg_info.err_pe = g_uart_err_pe;
                g_dbg_info.rx_overflow = (uint32_t)g_rx_buf.dropped;
                g_dbg_info.buf_len = (uint32_t)obuf_data_len(&g_rx_buf);
                g_dbg_info.parse_timeout = g_parse_timeout_cnt;
                {
                    const uint32_t interval = 1000;
                    uint8_t changed = (!g_last_dbg_valid) || debug_info_changed(&g_dbg_info, &g_last_dbg_info);
                    if (changed && (now - g_last_dbg_tick) >= interval) {
                        dashboard_debug_update(&g_dbg_info);
                        g_last_dbg_info = g_dbg_info;
                        g_last_dbg_valid = 1;
                        g_last_dbg_tick = now;
                    }
                }
            }
        }

        /* 解析空闲保护：若长时间未收到新字节且缓冲区仍有残留，则清空并计数 */
        {
            uint32_t now = HAL_GetTick();
            if (obuf_data_len(&g_rx_buf) > 0 && g_last_rx_byte_ms > 0) {
                if ((now - g_last_rx_byte_ms) > 1000) {
                    obuf_clear(&g_rx_buf);
                    g_parse_timeout_cnt++;
                }
            }
        }


        /* 串口接收看门狗：长时间无中断则重挂接收，防止接收停止 */
        {
            static uint32_t last_isr = 0;
            static uint32_t last_isr_tick = 0;
            uint32_t now = lv_tick_get();
            if ((now - last_isr_tick) >= 1000) {
                if (g_uart_isr_cnt == last_isr) {
                    HAL_UART_AbortReceive(&g_uart1_handle);
                    if (HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE) != HAL_OK) {
                        /* 若重挂接收失败，进行UART重初始化，防止卡死 */
                        HAL_UART_DeInit(&g_uart1_handle);
                        usart_init(UART_DEFAULT_BAUDRATE);
                    }
                } else {
                    last_isr = g_uart_isr_cnt;
                }
                last_isr_tick = now;
            }
        }

        /* 连接超时处理：超过2秒无有效帧则标记断开并清空缓冲 */
        {
            uint32_t now = HAL_GetTick();
            if (g_last_frame_ms > 0 && (now - g_last_frame_ms) > 2000) {
                g_metrics.port_connected = 0;
                obuf_clear(&g_rx_buf);
                g_last_frame_ms = 0;
            }
        }
#endif

        /* 主循环心跳：500ms 翻转 LED0，便于观察是否卡死 */
        {
            static uint32_t last_hb_tick = 0;
            uint32_t now = lv_tick_get();
            if ((now - last_hb_tick) >= 500) {
                last_hb_tick = now;
                LED0_TOGGLE();
            }
        }
        
        /* C. 短暂延时，防止CPU满载 */
        delay_ms(5);
    }
}

