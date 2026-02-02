/**
 * app.h - 应用程序头文件
 * 定义核心业务数据结构
 */
#ifndef APP_H
#define APP_H

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 核心业务数据结构（设备数据）
 * 包含 SQMWD 所需的随钻测量数据
 */
#define HISTORY_MAX_LEN 500

typedef struct {
    // SQMWD 核心业务数据
    float inclination;      // 井斜 (Degree)
    float azimuth;          // 方位 (Degree)
    float toolface;         // 工具面 (Degree, 0-360)
    float toolface_history[5]; // 最近5次工具面数据，用于同心圆环显示
    int   tf_type;          // 工具面类型 (5=GTF, 6=MTF) - 用于决定显示颜色或方式
    float pump_pressure;    // 泵压 (MPa)
    int   pump_status;      // 泵状态 (1=开泵, 0=停泵)
    
    // 通讯状态
    char  port_name[32];    // 串口名称 (e.g. "COM1")
    int   port_connected;   // 连接状态 (1=Connected, 0=Disconnected)

    // 原始数据/日志 (最新的一条)
    char  last_log_cmd[64]; // 最新的一条指令HEX串 (用于显示)
    char  last_decode_msg[64]; // 最新的一条解码信息 (用于显示)

    // 兼容原有结构
    uint8_t code;              
    uint8_t style;             
    float value;               
    float history[HISTORY_MAX_LEN]; 
    uint16_t history_len;       
    uint16_t history_pos;       
} plant_metrics_t;

/* 初始化 APP (创建 UI) */
void app_init(lv_disp_t *disp);

/* 停止模拟数据刷新 */
void app_stop_sim(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
