/**
 * @file lv_port_disp_templ.h
 * 显示驱动移植接口声明
 */

/* 复制本文件为 "lv_port_disp.h"，并将此值设为 "1" 启用内容 */
#if 1

#ifndef LV_PORT_DISP_TEMPL_H
#define LV_PORT_DISP_TEMPL_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      头文件
 *********************/
#include "lvgl/lvgl.h"

/*********************
 *      宏定义
 *********************/

/**********************
 *      类型定义
 **********************/

/**********************
 *      对外接口
 **********************/
void lv_port_disp_init(void); /* 显示设备初始化 */

/**********************
 *      宏
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PORT_DISP_TEMPL_H*/

#endif /* 启用/禁用内容 */
