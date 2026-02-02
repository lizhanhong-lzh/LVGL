
/**
 * @file lv_port_indev_templ.h
 * 触摸输入移植接口声明
 */

/* 复制本文件为 "lv_port_indev.h"，并将此值设为 "1" 启用内容 */
#if 1

#ifndef LV_PORT_INDEV_TEMPL_H
#define LV_PORT_INDEV_TEMPL_H

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
void lv_port_indev_init(void); /* 触摸设备初始化 */

/**********************
 *      宏
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_PORT_INDEV_TEMPL_H*/

#endif /* 启用/禁用内容 */
