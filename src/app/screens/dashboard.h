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

/* 追加一条解码表记录（与板端一致） */
void dashboard_append_decode_row(const char *name, float value, int highlight);

/* 弹出消息提示（auto_close_ms=0 表示不自动关闭） */
void dashboard_show_message(const char *text, uint32_t auto_close_ms);

#ifdef __cplusplus
}
#endif
