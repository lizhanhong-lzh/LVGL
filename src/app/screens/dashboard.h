/*
 * dashboard.h - 工业看板 UI（PC 模拟器 / 开发板共用同一套界面代码）
 */
#pragma once

#include "../app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 创建界面并返回屏幕对象（由调用方负责 lv_scr_load） */
lv_obj_t *dashboard_create(void);

/* 刷新界面数据 */
void dashboard_update(const plant_metrics_t *data);

/* 追加一条解码表记录 */
void dashboard_append_decode_row(const char *name, float value, int highlight);

/* 追加一条解码表记录（字符串值） */
void dashboard_append_decode_text_row(const char *name, const char *value_text, int highlight);

/* 调试信息结构体（用于界面左上角调试小部件） */
typedef struct {
	uint32_t rx_bytes;       /* 串口已接收字节数 */
	uint32_t rx_isr;         /* UART 中断进入次数 */
	uint32_t try_cnt;        /* 主循环心跳计数 */
	uint32_t frames_ok;      /* 成功解析帧数 */
	uint32_t frames_bad;     /* 校验失败帧数 */
	uint32_t rx_overflow;    /* 环形缓冲丢字节计数 */
		uint32_t buf_len;        /* 环形缓冲当前水位 */
	uint32_t parse_timeout;  /* 解析超时/清缓存计数 */
		uint32_t drop_no_header; /* 未找到帧头丢弃计数 */
		uint32_t drop_len;       /* 长度非法丢弃计数 */
		uint32_t drop_cmd;       /* 命令不匹配丢弃计数 */
		uint32_t drop_chk;       /* 校验失败丢弃计数 */
	uint32_t err_ore;        /* UART ORE 错误 */
	uint32_t err_fe;         /* UART FE 错误 */
	uint32_t err_ne;         /* UART NE 错误 */
	uint32_t err_pe;         /* UART PE 错误 */
	uint8_t  last_len;       /* 最近一次帧长度(LEN) */
	uint8_t  last_chk;       /* 最近一次帧校验(收到) */
	uint8_t  last_calc;      /* 最近一次帧校验(计算) */
	uint8_t  last_err;       /* 最近一次错误(0=无,1=校验) */
	uint8_t  last_sub_cmd;   /* 最近一次子命令 */
	char     last_name[32];  /* 最近一次参数名 */
	float    last_value;     /* 最近一次参数值 */
	char     last_raw[64];   /* 最近一次原始帧摘要(HEX) */
} dashboard_debug_info_t;

/* 更新调试小部件 */
void dashboard_debug_update(const dashboard_debug_info_t *info);

/* 弹出消息提示（auto_close_ms=0 表示不自动关闭） */
void dashboard_show_message(const char *text, uint32_t auto_close_ms);

/* 当前是否处于消息弹窗显示状态（用于暂停主界面刷新） */
int dashboard_message_is_active(void);

#ifdef __cplusplus
}
#endif
