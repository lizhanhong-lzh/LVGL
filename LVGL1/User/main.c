/**
s
 ****************************************************************************************************
 * @file        main.c
 * @author      正点原子团队(ALIENTEK) + 工业看板移植
 * @version     V2.0
 * @date        2026-01-23
 * @brief       STM32F767 工业数据显示系统（LVGL + SQMWD_Tablet 协议）
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * 模块说明
 * 模块名称: 主程序入口
 * 文件路径: d:\LVGL\LVGL1\User\main.c
 *
 * 功能概述:
 * 1. 系统初始化：时钟(216MHz)、串口、SDRAM、LCD、定时器等底层外设初始化。
 * 2. 协议解析：解析 SQMWD_Tablet 串口数据流。
 *    协议格式：头(2B) + CMD(1B) + LEN(1B) + Sub_CMD(1B) + Payload + XOR(1B)
 * 3. 数据分发：将解析出的物理量写入 g_metrics，并触发界面刷新。
 * 4. 主循环：驱动 LVGL 任务调度与串口轮询。
 ****************************************************************************************************
 */

/* 用户应用层头文件 */
#include "app/app.h"          /* 应用层主入口声明 (app_init) */
#include "app/obuf.h"         /* 环形缓冲区工具库 (Ring Buffer) */
#include "app/screens/dashboard.h" /* 仪表盘UI更新接口 */

#include <string.h>
#include <stdio.h>

/* 正点原子 BSP 驱动 */
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
#include "lv_port_indev_template.h" /* LVGL 触摸输入接口适配 */
#include "ff.h"                     /* FatFs */
/* NAND/FTL */
#include "./BSP/NAND/nand.h"
#include "./BSP/NAND/ftl.h"
/* 内存池使用率统计 */
#include "./SYSTEM/MALLOC/malloc.h"
/* lv_fs_fatfs 没有独立头文件，手动声明 */
void lv_fs_fatfs_init(void);
 

/* =============== 全局变量定义 =============== */

/* 串口接收底层缓冲区（16KB）
 * 说明：缓冲越大越不易在高峰期溢出，但会占用更多内存。
 */
static uint8_t g_rx_storage[16384];    

/* 环形缓冲区管理结构体（串口中断与主循环共享） */
obuf_t g_rx_buf;                      

/* 串口稳定性控制 */
static volatile uint32_t g_uart_ignore_until_ms = 0; /* 上电静默截止时间 */
static uint32_t g_last_frame_ms = 0;                 /* 最近一次有效帧时间 */
static volatile uint32_t g_last_rx_byte_ms = 0;       /* 最近一次收到字节时间 */

/* 解析超时统计 */
static uint32_t g_parse_timeout_cnt = 0;

/* 启动阶段标记（用于异常定位） */
volatile uint32_t g_boot_stage = 0;

/* SDRAM 简单读写自检（用于上电/复位稳定性） */
static int sdram_self_test(void)
{
    volatile uint32_t *p = (uint32_t *)0xC01F4000U; /* LVGL 堆起始地址 */
    uint32_t bak0 = p[0];
    uint32_t bak1 = p[1];
    uint32_t bak2 = p[2];
    uint32_t bak3 = p[3];

    p[0] = 0x55AA55AAu;
    p[1] = 0xAA55AA55u;
    p[2] = 0x12345678u;
    p[3] = 0x87654321u;

    if (p[0] != 0x55AA55AAu || p[1] != 0xAA55AA55u ||
        p[2] != 0x12345678u || p[3] != 0x87654321u) {
        return 0;
    }

    p[0] = bak0;
    p[1] = bak1;
    p[2] = bak2;
    p[3] = bak3;
    return 1;
}

/* =============== FATFS / NAND 文件接收 =============== */
static FATFS g_fatfs;
static uint8_t g_fatfs_mounted = 0;

/* ================= 串口模式开关 =================
 * 说明：为避免“文件写入命令”和“业务协议帧”互相干扰，
 *       这里提供显式模式开关。
 * - FILE 模式：处理 CMD/PUT 命令，用于 NAND 文件写入与管理。
 * - FRAME 模式：处理 SQMWD_Tablet 协议帧，用于实时数据刷新。
 * 默认值：FILE（便于首次写入字体/图片）。
 */
typedef enum {
    UART_MODE_FILE = 0,
    UART_MODE_FRAME
} uart_mode_t;

static uart_mode_t g_uart_mode = UART_MODE_FRAME;


typedef enum {
    FILE_RX_IDLE = 0,
    FILE_RX_DATA
} file_rx_state_t;

static file_rx_state_t g_file_rx_state = FILE_RX_IDLE;
static FIL g_file_rx;
static uint32_t g_file_rx_remain = 0;

/*
 * 挂载 NAND 到 FatFs (N:)
 * - 将 NAND 逻辑盘注册为 "N:"，供 FatFs 与 LVGL FS 使用
 * - 成功后置位 g_fatfs_mounted，允许 PUT/文件管理命令继续执行
 * - 失败时输出错误码并提示 CMD FMT，避免误操作导致死循环
 * - 仅执行一次挂载，不做重试/格式化，防止误擦除
 */
static int fatfs_mount_once(void)
{
    FRESULT res = f_mount(&g_fatfs, "N:", 1);
    if (res == FR_OK) {
        g_fatfs_mounted = 1;
        printf("[FATFS] Mount N: OK\r\n");
        return 1;
    }

    g_fatfs_mounted = 0;
    printf("[FATFS] Mount N: FAIL (%d)\r\n", (int)res);
    printf("[FATFS] Send: CMD FMT  (format NAND)\r\n");
    return 0;
}

/*
 * 格式化 NAND (FatFs)
 * - 仅做文件系统格式化（f_mkfs），不重建 FTL
 * - 适用于首次使用或文件系统异常
 * - 完成后自动重新挂载，确保后续命令可用
 * - 通过串口 CMD FMT 触发
 */
static void fatfs_format(void)
{
    FRESULT res;

    printf("[FATFS] Formatting...\r\n");
    res = f_mkfs("N:", 0, 0);
    if (res == FR_OK) {
        printf("[FATFS] Format OK\r\n");
    } else {
        printf("[FATFS] Format FAIL (%d)\r\n", (int)res);
    }

    fatfs_mount_once();
}

/*
 * 从环形缓冲区读取一行命令
 * - 用于 FILE 模式下解析 CMD/PUT 的文本行
 * - 兼容 \r、\n、\r\n，读到行尾才返回
 * - 成功返回 1，失败（未形成完整行）返回 0
 * - 会去除行尾换行符，便于上层直接 strcmp/strncmp
 * - 若无换行符则不消耗缓冲区数据
 */
static int obuf_try_read_line(obuf_t *in, char *out, size_t cap)
{
    size_t len = obuf_data_len(in);
    size_t i;

    if (cap == 0) return 0;
    for (i = 0; i < len && i + 1 < cap; i++) {
        int c = obuf_peek(in, i);
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            size_t n = i + 1;
            obuf_read(in, (uint8_t *)out, n);
            out[n] = '\0';

            /* 吃掉紧随其后的 \n (处理 \r\n) */
            if (c == '\r' && obuf_data_len(in) > 0) {
                int c2 = obuf_peek(in, 0);
                if (c2 == '\n') {
                    uint8_t dummy;
                    obuf_read(in, &dummy, 1);
                }
            }

            while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
                out[n - 1] = '\0';
                n--;
            }
            return 1;
        }
    }
    return 0;
}

/*
 * 文件接收状态机
 * - 触发：收到 PUT <path> <size> 后进入 FILE_RX_DATA
 * - 从环形缓冲区分块读取并写入 FatFs 文件
 * - 每次尽量按 512B 写入，兼容不完整包
 * - 写入失败立即关闭文件并回到 IDLE，避免文件损坏
 * - 写完后回到 IDLE，打印完成提示
 */
static void process_file_rx(void)
{
    uint8_t buf[512];

    if (g_file_rx_state != FILE_RX_DATA) return;

    while (g_file_rx_remain > 0) {
        size_t avail = obuf_data_len(&g_rx_buf);
        UINT to_read;
        UINT written = 0;

        if (avail == 0) return;

        to_read = (UINT)avail;
        if (to_read > sizeof(buf)) to_read = sizeof(buf);
        if (to_read > g_file_rx_remain) to_read = (UINT)g_file_rx_remain;

        obuf_read(&g_rx_buf, buf, to_read);
        if (f_write(&g_file_rx, buf, to_read, &written) != FR_OK || written != to_read) {
            printf("[FATFS] PUT write failed\r\n");
            f_close(&g_file_rx);
            g_file_rx_state = FILE_RX_IDLE;
            g_file_rx_remain = 0;
            return;
        }

        g_file_rx_remain -= written;
    }

    f_close(&g_file_rx);
    g_file_rx_state = FILE_RX_IDLE;
    printf("[FATFS] PUT done\r\n");
}

/*
 * 串口命令解析入口 (仅 FILE 模式执行)
 * - 支持 PUT 与 CMD 管理指令
 * - 自动在缓冲区查找 "PUT "/"CMD "，丢弃前置噪声
 * - PUT：进入文件接收状态机，后续字节按原始文件数据处理
 * - CMD：执行挂载/格式化/目录操作/NAND 扫描等管理动作
 * - 解析失败仅提示，不影响后续数据流
 */
static void process_uart_commands(void)
{
    char line[160];
    int b0, b1, b2, b3;
    static const uint8_t put_pat[4] = {'P','U','T',' '};
    static const uint8_t cmd_pat[4] = {'C','M','D',' '};

    if (g_file_rx_state == FILE_RX_DATA) return;

    if (obuf_data_len(&g_rx_buf) < 4) return;

    /* 丢弃前置噪声，确保命令从缓冲区起始处对齐 */
    {
        int off_put = obuf_find(&g_rx_buf, put_pat, sizeof(put_pat));
        int off_cmd = obuf_find(&g_rx_buf, cmd_pat, sizeof(cmd_pat));
        int off = -1;

        if (off_put >= 0 && off_cmd >= 0) off = (off_put < off_cmd) ? off_put : off_cmd;
        else if (off_put >= 0) off = off_put;
        else if (off_cmd >= 0) off = off_cmd;

        if (off > 0) {
            obuf_drop(&g_rx_buf, (size_t)off);
        }
    }

    if (obuf_data_len(&g_rx_buf) < 4) return;
    b0 = obuf_peek(&g_rx_buf, 0);
    b1 = obuf_peek(&g_rx_buf, 1);
    b2 = obuf_peek(&g_rx_buf, 2);
    b3 = obuf_peek(&g_rx_buf, 3);

    /*
     * PUT <path> <size>
     * 例: PUT N:/font/my_font_20.bin 18388
     * 之后发送 size 个原始字节
     */
    if (b0 == 'P' && b1 == 'U' && b2 == 'T' && b3 == ' ') {
        if (!obuf_try_read_line(&g_rx_buf, line, sizeof(line))) return;

        char path[96];
        char size_str[32];
        unsigned long size = 0;
        char *p = line;
        char *q;

        /* skip leading spaces */
        while (*p == ' ') p++;
        if (strncmp(p, "PUT", 3) != 0) {
            printf("[FATFS] PUT format error\r\n");
            return;
        }
        p += 3;
        while (*p == ' ') p++;

        /* path */
        q = path;
        while (*p && *p != ' ' && (q - path) < (int)sizeof(path) - 1) {
            *q++ = *p++;
        }
        *q = '\0';
        while (*p == ' ') p++;

        /* size string */
        q = size_str;
        while (*p && *p != ' ' && (q - size_str) < (int)sizeof(size_str) - 1) {
            if (*p != '<' && *p != '>') {
                *q++ = *p;
            }
            p++;
        }
        *q = '\0';

        if (path[0] == '\0' || size_str[0] == '\0') {
            printf("[FATFS] PUT format error\r\n");
            return;
        }

        size = strtoul(size_str, NULL, 10);
        if (size == 0) {
            printf("[FATFS] PUT size error\r\n");
            return;
        }

        /* 未挂载时禁止写入 */
        if (!g_fatfs_mounted) {
            printf("[FATFS] Not mounted, send CMD MOUNT or CMD FMT\r\n");
            return;
        }

        if (f_open(&g_file_rx, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
            printf("[FATFS] Open failed: %s\r\n", path);
            return;
        }

        g_file_rx_state = FILE_RX_DATA;
        g_file_rx_remain = (uint32_t)size;
        printf("[FATFS] PUT start: %s (%lu bytes)\r\n", path, size);
        return;
    }

    /* CMD <...> 管理命令 */
    if (b0 == 'C' && b1 == 'M' && b2 == 'D' && b3 == ' ') {
        if (!obuf_try_read_line(&g_rx_buf, line, sizeof(line))) return;

        if (strcmp(line, "CMD FMT") == 0) {
            fatfs_format();
        } else if (strcmp(line, "CMD MOUNT") == 0) {
            fatfs_mount_once();
        } else if (strncmp(line, "CMD MKDIR ", 10) == 0) {
            const char *path = line + 10;
            if (*path == '\0') {
                printf("[FATFS] MKDIR format error\r\n");
            } else {
                FRESULT r = f_mkdir(path);
                if (r == FR_OK || r == FR_EXIST) {
                    printf("[FATFS] MKDIR OK\r\n");
                } else {
                    printf("[FATFS] MKDIR FAIL (%d)\r\n", (int)r);
                }
            }
        } else if (strncmp(line, "CMD STAT ", 9) == 0) {
            const char *path = line + 9;
            if (*path == '\0') {
                printf("[FATFS] STAT format error\r\n");
            } else {
                FILINFO fno;
                FRESULT r = f_stat(path, &fno);
                if (r == FR_OK) {
                    printf("[FATFS] STAT OK size=%lu attr=0x%02X\r\n",
                           (unsigned long)fno.fsize, (unsigned)fno.fattrib);
                } else {
                    printf("[FATFS] STAT FAIL (%d)\r\n", (int)r);
                }
            }
        } else if (strncmp(line, "CMD LS ", 7) == 0) {
            const char *path = line + 7;
            if (*path == '\0') {
                printf("[FATFS] LS format error\r\n");
            } else {
                DIR dir;
                FILINFO fno;
                FRESULT r = f_opendir(&dir, path);
                if (r != FR_OK) {
                    printf("[FATFS] LS FAIL (%d)\r\n", (int)r);
                } else {
                    printf("[FATFS] LS %s\r\n", path);
                    for (;;) {
                        r = f_readdir(&dir, &fno);
                        if (r != FR_OK || fno.fname[0] == 0) break;
                        printf("  %s  %lu\r\n", fno.fname, (unsigned long)fno.fsize);
                    }
                    f_closedir(&dir);
                }
            }
        } else if (strncmp(line, "CMD DEL ", 8) == 0) {
            const char *path = line + 8;
            if (*path == '\0') {
                printf("[FATFS] DEL format error\r\n");
            } else {
                FRESULT r = f_unlink(path);
                if (r == FR_OK) {
                    printf("[FATFS] DEL OK\r\n");
                } else {
                    printf("[FATFS] DEL FAIL (%d)\r\n", (int)r);
                }
            }
        } else if (strcmp(line, "CMD NANDSCAN") == 0) {
            printf("[NAND] Scan bad blocks...\r\n");
            u32 good = FTL_SearchBadBlock();
            u32 total = nand_dev.block_totalnum;
            u32 bad = (good <= total) ? (total - good) : 0;
            printf("[NAND] Good blocks: %lu\r\n", (unsigned long)good);
            printf("[NAND] Bad blocks: %lu\r\n", (unsigned long)bad);
            printf("[NAND] total=%u good=%u valid=%u\r\n",
                   (unsigned) nand_dev.block_totalnum,
                   (unsigned) nand_dev.good_blocknum,
                   (unsigned) nand_dev.valid_blocknum);
        } else if (strcmp(line, "CMD NANDFMT") == 0) {
            printf("[NAND] FTL format...\r\n");
            u8 r = FTL_Format();
            printf("[NAND] FTL format %s\r\n", (r == 0) ? "OK" : "FAIL");
            fatfs_mount_once();
        } else if (strncmp(line, "CMD MODE ", 9) == 0) {
            const char *mode = line + 9;
            if (strcmp(mode, "FILE") == 0) {
                g_uart_mode = UART_MODE_FILE;
                printf("[UART] MODE FILE (CMD/PUT)\r\n");
            } else if (strcmp(mode, "FRAME") == 0) {
                g_uart_mode = UART_MODE_FRAME;
                printf("[UART] MODE FRAME (protocol)\r\n");
            } else {
                printf("[UART] MODE format error\r\n");
            }
        } else if (strcmp(line, "CMD HELP") == 0) {
            printf("[FATFS] CMD FMT   -> format NAND\r\n");
            printf("[FATFS] CMD MOUNT -> mount N:\r\n");
            printf("[FATFS] CMD MKDIR <path> -> mkdir\r\n");
            printf("[FATFS] CMD STAT <path>  -> file info\r\n");
            printf("[FATFS] CMD LS <path>    -> list dir\r\n");
            printf("[FATFS] CMD DEL <path>   -> delete file/dir\r\n");
            printf("[NAND]  CMD NANDSCAN -> scan bad blocks\r\n");
            printf("[NAND]  CMD NANDFMT  -> FTL format\r\n");
            printf("[UART]  CMD MODE FILE    -> file mode\r\n");
            printf("[UART]  CMD MODE FRAME   -> protocol mode\r\n");
            printf("[FATFS] CMD FONTHEAD <path> -> dump first 32 bytes\r\n");
            printf("[FATFS] PUT <path> <size> then send raw bytes\r\n");
        } else if (strncmp(line, "CMD FONTHEAD ", 13) == 0) {
            const char *path = line + 13;
            if (*path == '\0') {
                printf("[FATFS] FONTHEAD format error\r\n");
            } else {
                FIL f;
                UINT br = 0;
                uint8_t head[32];
                if (f_open(&f, path, FA_READ) != FR_OK) {
                    printf("[FATFS] FONTHEAD open FAIL: %s\r\n", path);
                } else {
                    FRESULT r = f_read(&f, head, sizeof(head), &br);
                    f_close(&f);
                    if (r != FR_OK || br == 0) {
                        printf("[FATFS] FONTHEAD read FAIL: %s\r\n", path);
                    } else {
                        printf("[FATFS] FONTHEAD %s (%u bytes):\r\n", path, (unsigned)br);
                        for (UINT i = 0; i < br; i++) {
                            printf("%02X%s", head[i], ((i + 1) % 16 == 0) ? "\r\n" : " ");
                        }
                        if (br % 16 != 0) {
                            printf("\r\n");
                        }
                    }
                }
            }
        } else {
            printf("[FATFS] Unknown CMD\r\n");
        }
    }
}


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

    /*
     * 【通信状态关键点#1：原始字节到达】
     * 只要任意字节到达（不要求是完整帧），就刷新“最近收到字节”的时间戳。
     * 该时间戳用于后续 UI 的“通信中/通信超时”判断。
     */
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

/* 按名称文本匹配字段（与 SQMWD_Tablet 的正则规则保持一致的含义） */
static int match_field_by_name(const char *name, field_match_t *m)
{
    if (!name || !m || name[0] == '\0') {
        return 0;
    }

    /* 优先匹配 GTF / MTF，避免被 TF 误命中 */
    if (strstr(name, "GTF") || strstr(name, "gtf") || strstr(name, "重力工具面")) {
        m->kind = FIELD_GTF;
        return 1;
    }
    if (strstr(name, "MTF") || strstr(name, "mtf") || strstr(name, "磁性工具面")) {
        m->kind = FIELD_MTF;
        return 1;
    }
    if (strstr(name, "TF") || strstr(name, "tf") || strstr(name, "工具面")) {
        m->kind = FIELD_TF;
        return 1;
    }
    if (strstr(name, "INC") || strstr(name, "inc") || strstr(name, "井斜")) {
        m->kind = FIELD_INC;
        return 1;
    }
    if (strstr(name, "AZI") || strstr(name, "azi") || strstr(name, "方位")) {
        m->kind = FIELD_AZI;
        return 1;
    }

    return 0;
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

/*
 * 主入口
 * - 初始化时钟、SDRAM、LCD、UART、定时器等底层硬件
 * - 初始化 LVGL 与 FatFs，创建看板 UI
 * - 主循环中处理串口命令/文件接收/协议解析，并节流刷新 UI
 * - 通过 LED 心跳与调试计数监控运行状态
 */
int main(void)
{
    g_boot_stage = 1;                         /* 进入 main */
    /* ========== 1. 硬件底层初始化 ========== */
    g_boot_stage = 10;                        /* Cache/时钟初始化前 */
    sys_cache_enable();                         /* 打开L1-Cache (提升性能) */
    HAL_Init();                                 /* 初始化HAL库 */
    sys_stm32_clock_init(432, 25, 2, 9);        /* 配置系统时钟: 216MHz */
    delay_init(216);                            /* 初始化延时函数 */

    g_boot_stage = 20;                        /* 串口/缓存/LED前 */
    /* 初始化协议接收环形缓冲区（必须在串口接收中断开始写入前完成） */
    obuf_init(&g_rx_buf, g_rx_storage, sizeof(g_rx_storage));

    usart_init(UART_DEFAULT_BAUDRATE);          /* 初始化串口 (接收电脑数据) */
    usart3_init(UART_DEFAULT_BAUDRATE);         /* 初始化 USART3 (LoRa) */
    g_uart_ignore_until_ms = HAL_GetTick() + 300; /* 300ms 静默期过滤串口毛刺 */
    led_init();                                 /* 初始化LED指示灯 */
    g_boot_stage = 30;                        /* MPU/SDRAM/LCD前 */
    mpu_memory_protection();                    /* 配置MPU保护 */
    sdram_init();                               /* 初始化SDRAM (显存) */
    /* 上电/复位偶发不稳定时，进行一次重试 */
    if (!sdram_self_test()) {
        delay_ms(10);
        sdram_init();
        delay_ms(10);
        if (!sdram_self_test()) {
            printf("[SDRAM] init check failed\r\n");
        }
    }
    my_mem_init(SRAMEX);                        /* 初始化外部SDRAM内存池统计 */
    my_mem_init(SRAMDTCM);                      /* 初始化DTCM内存池统计 */
    delay_ms(10);                               /* 给 LCD 上电稳定时间 */
    lcd_init();                                 /* 初始化LCD屏幕 *** 必须在lv_init前 *** */
    lcd_display_dir(1);                         /* 设置显示方向（与 LVGL 端口保持一致） */
    btim_timx_int_init(10-1, 10800-1);          /* 初始化定时器 (为LVGL提供1ms心跳) */
    
    /* ========== 2. LVGL图形库初始化 ========== */
    g_boot_stage = 40;                        /* LVGL 初始化 */
    lv_init();                                  /* LVGL核心初始化 */
    lv_port_disp_init();                        /* 显示接口初始化 */
    lv_port_indev_init();                       /* 触摸输入设备初始化 */
    lv_fs_fatfs_init();                          /* 注册 LVGL 的 FatFs 驱动 */
    fatfs_mount_once();                          /* 挂载 NAND (N:) */
    
    /* ========== 3. 用户应用初始化 ========== */
    g_boot_stage = 50;                        /* UI 创建 */
    app_init(NULL);                             /* 创建工业看板UI（disp 参数预留，板端填 NULL） */
    
    /* ========== 4. 主循环 (无限) ========== */
    g_boot_stage = 100;                       /* 进入主循环 */
    while(1)
    {
        g_dbg_info.try_cnt++;
        usart_rx_recover_if_needed();

        /* 串口模式切换：命令始终可用，文件数据只在 FILE 模式处理 */
        process_uart_commands();
        if (g_uart_mode == UART_MODE_FILE) {
            process_file_rx();
        }

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

    /* FRAME 模式才允许解析业务协议帧 */
    if (g_uart_mode == UART_MODE_FRAME)

        /* 
         * [优化]: 改为 while 循环，尽可能多地通过本轮循环消化缓冲区积压的数据。
         * 限制单次最大处理 50 包，防止数据量过大导致 UI 线程被饿死 (Watchdog 超时或界面卡顿)。
         */
        while (process_cnt < 100 && sx_try_parse_one(&g_rx_buf, &frame))
        {
            process_cnt++;

            /* 更新连接状态：只要收到有效帧就认为已连接 */
            g_metrics.port_connected = 1;
            {
                /*
                 * 【串口连接显示逻辑】
                 * 根据最近一次接收的端口来源(USART2/USART3)决定 UI 上的端口名称。
                 * 如果 LoRa 数据来自 USART3，则显示 UART3/COM3；否则显示 UART2/COM2。
                 */
                uart_rx_source_t src = usart_get_last_rx_port();
                if (src == UART_SRC_USART3) {
                    strncpy(g_metrics.port_name, "UART3", sizeof(g_metrics.port_name) - 1);
                } else {
                    strncpy(g_metrics.port_name, "UART2", sizeof(g_metrics.port_name) - 1);
                }
                g_metrics.port_name[sizeof(g_metrics.port_name) - 1] = '\0';
            }

            if (frame.cmd == 0x09 && frame.sub_cmd == 0x01 && frame.has_f2) {
                float press = (frame.f1 > 0.0f) ? frame.f1 : frame.f2;
                g_metrics.pump_pressure = press;
                /*
                 * 泵压开关阈值：> 0.7 视为“开泵”，<= 0.7 视为“关泵”。
                 * 与 SQMWD_Tablet 的 MainWindow::setPumpStatus() 保持一致。
                 */
                g_metrics.pump_status = (press > 0.7f) ? 1 : 0;
                g_metrics.pump_pressure_valid = 1;
                g_metrics.last_update_id = UPDATE_PUMP;

                /* 调试信息更新 */
                g_dbg_info.last_sub_cmd = 0x01;
                strncpy(g_dbg_info.last_name, "泵压", sizeof(g_dbg_info.last_name) - 1);
                g_dbg_info.last_name[sizeof(g_dbg_info.last_name) - 1] = '\0';
                g_dbg_info.last_value = press;
            } else if (frame.cmd == 0x09 && frame.sub_cmd == 0x02) {
                field_match_t match = {FIELD_NONE, 0};
                const char *show_name = "";
                const char *fid_name = NULL;

                /* 解析主逻辑：优先按FID映射字段类型 */
                if (frame.has_fid && match_field_by_fid(frame.fid, &match, &fid_name)) {
                    show_name = fid_name;
                }

                /* 名称字符串仅用于显示兜底，不参与解析 */
                if (show_name[0] == '\0') {
                    show_name = frame.has_text ? frame.text : "";
                }

                /* 若FID未匹配，使用名称文本做兜底解析 */
                if (match.kind == FIELD_NONE) {
                    match_field_by_name(show_name, &match);
                }

                /* 解析线程仅缓存“最近一条 + 时间戳”，避免堆积 */
                strncpy(g_decode_name, show_name, sizeof(g_decode_name) - 1);
                g_decode_name[sizeof(g_decode_name) - 1] = '\0';
                g_decode_value = frame.f1;
                g_decode_highlight = 1;
                g_decode_last_ms = lv_tick_get();

                /* 调试信息更新 */
                g_dbg_info.last_sub_cmd = 0x02;
                strncpy(g_dbg_info.last_name, show_name, sizeof(g_dbg_info.last_name) - 1);
                g_dbg_info.last_name[sizeof(g_dbg_info.last_name) - 1] = '\0';
                g_dbg_info.last_value = frame.f1;

                /* 按字段类型写入业务数据 */
                if (match.kind == FIELD_INC) {
                    g_metrics.inclination = frame.f1;
                    g_metrics.last_update_id = UPDATE_INC;
                } else if (match.kind == FIELD_AZI) {
                    g_metrics.azimuth = frame.f1;
                    g_metrics.last_update_id = UPDATE_AZI;
                } else if (match.kind == FIELD_GTF) {
                    g_metrics.toolface = frame.f1;
                    g_metrics.tf_type = 0x13;
                    g_metrics.last_update_id = UPDATE_TF;
                } else if (match.kind == FIELD_MTF) {
                    g_metrics.toolface = frame.f1;
                    g_metrics.tf_type = 0x14;
                    g_metrics.last_update_id = UPDATE_TF;
                } else if (match.kind == FIELD_TF) {
                    g_metrics.toolface = frame.f1;
                    g_metrics.tf_type = 0x00;
                    g_metrics.last_update_id = UPDATE_TF;
                }

                if (match.kind == FIELD_TF || match.kind == FIELD_GTF || match.kind == FIELD_MTF) {
                    for (int i = 0; i < 4; i++) {
                        g_metrics.toolface_history[i] = g_metrics.toolface_history[i + 1];
                        g_metrics.toolface_type_history[i] = g_metrics.toolface_type_history[i + 1];
                    }
                    g_metrics.toolface_history[4] = g_metrics.toolface;
                    g_metrics.toolface_type_history[4] = (uint8_t)g_metrics.tf_type;
                }
            } else if (frame.cmd == 0x09 && frame.sub_cmd == 0x03) {
                if (frame.has_text) {
                    uint32_t ms = 0;
                    if (frame.auto_close_sec > 0.0f) {
                        ms = (uint32_t)(frame.auto_close_sec * 1000.0f + 0.5f);
                    }
                    printf("[MSG] auto_close_ms=%lu text=%s\r\n",
                           (unsigned long)ms,
                           frame.text);
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

        /* 通信超时判断：10秒内无新字节=通信中（统一作为“通信正常/连接正常”） */
        {
            uint32_t now = HAL_GetTick();
            uint8_t alive = 0;
            if (g_comm_last_rx_ms != 0U) {
                /*
                 * 【通信状态关键点#2：超时判定】
                 * - 10 秒内收到任意字节：alive=1（通信中）
                 * - 超过 12 秒未收到任何字节：alive=0（通信超时）
                 * - 10~12 秒之间保持上一次状态，避免状态来回抖动
                 */
                uint32_t dt = now - g_comm_last_rx_ms;
                if (dt < 10000U) {
                    alive = 1;
                } else if (dt > 12000U) {
                    alive = 0;
                } else {
                    alive = g_metrics.comm_alive; /* 保持上一次状态 */
                }
            }
            if (g_metrics.comm_alive != alive) {
                g_metrics.comm_alive = alive;
                g_ui_dirty = 1;
            }

            if (g_metrics.port_connected != alive) {
                g_metrics.port_connected = alive;
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

                /* 内存占用打印（需要时再启用，避免长期刷串口影响性能） */
                /*
                {
                    uint16_t sram_in = my_mem_perused(SRAMIN);
                    uint16_t sram_ex = my_mem_perused(SRAMEX);
                    uint16_t sram_dtcm = my_mem_perused(SRAMDTCM);
                    lv_mem_monitor_t mon;
                    lv_mem_monitor(&mon);
                    printf("[MEM] SRAM=%u.%u%%  SDRAM=%u.%u%%  DTCM=%u.%u%%\r\n",
                           sram_in / 10, sram_in % 10,
                           sram_ex / 10, sram_ex % 10,
                           sram_dtcm / 10, sram_dtcm % 10);
                    printf("[LVGL] total=%lu free=%lu used=%u%%\r\n",
                           (unsigned long)mon.total_size,
                           (unsigned long)mon.free_size,
                           (unsigned)mon.used_pct);

                    // 简单趋势检测：LVGL 空闲持续下降时提示
                    {
                        static uint32_t last_free = 0xFFFFFFFFu;
                        if (last_free != 0xFFFFFFFFu && mon.free_size + 1024u < last_free) {
                            printf("[LVGL] WARN: free drop %lu -> %lu\r\n",
                                   (unsigned long)last_free,
                                   (unsigned long)mon.free_size);
                        }
                        last_free = mon.free_size;
                    }
                }
                */
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

        /* 已取消“基于有效帧”的断开逻辑，避免与字节级通信状态冲突 */
#endif

        if (g_ui_dirty) {
            dashboard_update(&g_metrics);
            g_ui_dirty = 0;
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

