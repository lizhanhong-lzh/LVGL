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
static uint8_t g_rx_storage[16384];    

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

static uint8_t g_ui_dirty = 0;
static uint32_t g_last_dbg_tick = 0;
static uint32_t g_last_decode_tick = 0;
/* 解码表刷新缓存：仅保留最近一条 + 时间戳 */
static char g_decode_name[32];
static float g_decode_value = 0.0f;
static uint8_t g_decode_highlight = 0;
static uint32_t g_decode_last_ms = 0;
/* 通信活跃检测（10秒内是否收到数据） */
static uint32_t g_comm_last_rx_ms = 0;
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
    g_comm_last_rx_ms = g_last_rx_byte_ms;

    /* 调试：统计接收字节数 */
    g_dbg_info.rx_bytes++;

    /* 2. 快速连通性验证 (LED Flash) */
    static uint16_t rx_cnt = 0;
    if (++rx_cnt >= 200) {
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

    int off = obuf_find(in, header, sizeof(header));
    if (off < 0) {
        size_t len = obuf_data_len(in);
        if (len > 1) {
            obuf_drop(in, len - 1);
        }
        g_dbg_info.drop_no_header++;
        return 0;
    }

    if (off > 0) {
        obuf_drop(in, (size_t)off);
    }

    if (obuf_data_len(in) < 5) {
        return 0;
    }

    int cmd = obuf_peek(in, 2);
    int len = obuf_peek(in, 3);
    if (cmd < 0 || len < 0) {
        return 0;
    }

    if ((uint8_t)cmd != 0x09) {
        g_dbg_info.drop_cmd++;
        obuf_drop(in, 1);
        return 0;
    }

    if ((uint8_t)len == 0 || (uint8_t)len > 200) {
        g_dbg_info.frames_bad++;
        g_dbg_info.drop_len++;
        obuf_drop(in, 1);
        return 0;
    }

    size_t frame_len = (size_t)((uint8_t)len) + 5;
    if (obuf_data_len(in) < frame_len) {
        return 0;
    }

    /* XOR 校验（保留，但不做额外的调试填充） */
    uint8_t calc = 0;
    for (size_t i = 0; i < frame_len - 1; i++) {
        int b = obuf_peek(in, i);
        if (b < 0) return 0;
        calc ^= (uint8_t)b;
    }
    int chk = obuf_peek(in, frame_len - 1);
    if (chk < 0) return 0;
    if ((uint8_t)chk != calc) {
        g_dbg_info.frames_bad++;
        g_dbg_info.drop_chk++;
        obuf_drop(in, 1);
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->cmd = (uint8_t)cmd;
    out->sub_cmd = (uint8_t)obuf_peek(in, 4);

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

    obuf_drop(in, frame_len);
    g_dbg_info.frames_ok++;
    g_last_frame_ms = HAL_GetTick();
    return 1;
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
            if ((now - last_lvgl_tick) >= 200) {
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
        while (process_cnt < 100 && sx_try_parse_one(&g_rx_buf, &frame))
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
            g_ui_dirty = 1;
        }

        /* 通信超时判断：10秒内无新数据则提示超时 */
        {
            uint32_t now = HAL_GetTick();
            uint8_t alive = (g_comm_last_rx_ms != 0U && (now - g_comm_last_rx_ms) < 10000U) ? 1 : 0;
            if (g_metrics.comm_alive != alive) {
                g_metrics.comm_alive = alive;
                g_ui_dirty = 1;
            }
        }

        /* 低频刷新调试面板 */
        {
            uint32_t now = lv_tick_get();
            if ((now - g_last_dbg_tick) >= 1000) {
                g_last_dbg_tick = now;
                g_dbg_info.rx_isr = g_uart_isr_cnt;
                g_dbg_info.err_ore = g_uart_err_ore;
                g_dbg_info.err_fe = g_uart_err_fe;
                g_dbg_info.err_ne = g_uart_err_ne;
                g_dbg_info.err_pe = g_uart_err_pe;
                g_dbg_info.rx_overflow = (uint32_t)g_rx_buf.dropped;
                g_dbg_info.buf_len = (uint32_t)obuf_data_len(&g_rx_buf);
                g_dbg_info.parse_timeout = g_parse_timeout_cnt;
                dashboard_debug_update(&g_dbg_info);
            }
        }

        /* 串口接收看门狗：长时间无中断则重挂接收 */
        {
            static uint32_t last_isr = 0;
            static uint32_t last_isr_tick = 0;
            uint32_t now = lv_tick_get();
            if ((now - last_isr_tick) >= 2000) {
                if (g_uart_isr_cnt == last_isr) {
                    HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
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
                g_ui_dirty = 1;
            }
        }
#endif

        if (g_ui_dirty) {
            if (!dashboard_message_is_active()) {
                dashboard_update(&g_metrics);
                g_ui_dirty = 0;
            }
        }

        /* 主循环心跳：500ms 翻转 LED0，便于观察是否卡死 */
        {
            static uint32_t last_hb_tick = 0;
            uint32_t now = lv_tick_get();
            if ((now - last_hb_tick) >= 1000) {
                last_hb_tick = now;
                LED0_TOGGLE();
            }
        }
        
        /* C. 短暂延时，防止CPU满载 */
        delay_ms(5);
    }
}

