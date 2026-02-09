/**
 * @file lv_port_disp_templ.c
 * 显示驱动移植实现
 */

/* 复制本文件为 "lv_port_disp.c"，并将此值设为 "1" 启用内容 */
#if 1

/*********************
 *      头文件
 *********************/
#include "lv_port_disp_template.h"
#include "../../lvgl.h"
#include "./BSP/LCD/lcd.h"

/*********************
 *      宏定义
 *********************/

/**********************
 *      类型定义
 **********************/

/**********************
 *  静态函数声明
 **********************/
static void disp_init(void); /* LCD 初始化 */

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p); /* 刷屏回调 */
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//        const lv_area_t * fill_area, lv_color_t color);

/**********************
 *  静态变量
 **********************/

/**********************
 *      宏
 **********************/

/**********************
 *   对外接口
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * 初始化显示硬件
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * 创建绘图缓冲区
     *----------------------------*/

    /* 示例 1：单缓冲 */
    static lv_disp_draw_buf_t draw_buf_dsc_1;
    static lv_color_t buf_1[1200 * 10];                          /* 10 行缓冲 */
    lv_disp_draw_buf_init(&draw_buf_dsc_1, buf_1, NULL, 1200 * 10);   /* 初始化显示缓冲 */

    /* 示例 2：双缓冲（当前未启用） */
    //static lv_disp_draw_buf_t draw_buf_dsc_2;
    //static lv_color_t buf_2_1[MY_DISP_HOR_RES * 10];                        /* 10 行缓冲 */
    //static lv_color_t buf_2_2[MY_DISP_HOR_RES * 10];                        /* 另一块 10 行缓冲 */
    //lv_disp_draw_buf_init(&draw_buf_dsc_2, buf_2_1, buf_2_2, MY_DISP_HOR_RES * 10);   /* 初始化显示缓冲 */

    /* 示例 3：全屏双缓冲（需设置 full_refresh=1） */
    //static lv_disp_draw_buf_t draw_buf_dsc_3;
    //static lv_color_t buf_3_1[MY_DISP_HOR_RES * MY_DISP_VER_RES];            /* 全屏缓冲 */
    //static lv_color_t buf_3_2[MY_DISP_HOR_RES * MY_DISP_VER_RES];            /* 另一块全屏缓冲 */
    //lv_disp_draw_buf_init(&draw_buf_dsc_3, buf_3_1, buf_3_2, MY_DISP_VER_RES * LV_VER_RES_MAX);   /* 初始化显示缓冲 */

    /*-----------------------------------
     * 在 LVGL 中注册显示设备
     *----------------------------------*/

    static lv_disp_drv_t disp_drv;                         /* 显示驱动描述符 */
    lv_disp_drv_init(&disp_drv);                    /* 基础初始化 */

    /* 设置访问显示硬件的回调 */

    /* 设置显示分辨率（来自 lcd 驱动） */
    disp_drv.hor_res = lcddev.width;
    disp_drv.ver_res = lcddev.height;

    /* 刷屏回调：把缓冲区内容刷到 LCD */
    disp_drv.flush_cb = disp_flush;

    /* 设置显示缓冲 */
    disp_drv.draw_buf = &draw_buf_dsc_1;

    /* 示例 3 需要打开 full_refresh */
    //disp_drv.full_refresh = 1

    /* 如果有 GPU，可用回调加速填充。
     * LVGL 内置 GPU 可在 lv_conf.h 中开启；
     * 其他 GPU 可自行实现此回调。 */
    //disp_drv.gpu_fill_cb = gpu_fill;

    /* 最终注册驱动 */
    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   静态函数
 **********************/

/* 初始化 LCD 及相关外设 */
static void disp_init(void)
{
    /* LCD 已在 main.c 中初始化，这里不再重复初始化，避免偶发显示异常 */
}

/* 将内部缓冲区指定区域刷新到 LCD
 * 可用 DMA 或硬件加速异步处理，但完成后必须调用 lv_disp_flush_ready(). */
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    /* 直接填充 LCD 区域（阻塞式，简单稳定） */
	lcd_color_fill(area->x1, area->y1, area->x2, area->y2, (uint16_t*)color_p);
    lv_disp_flush_ready(disp_drv);
}

/* 可选：GPU 接口 */

/* 如果 MCU 有 GPU，可用于加速填充 */
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//                    const lv_area_t * fill_area, lv_color_t color)
//{
//    /* 示例代码：实际应由 GPU 完成 */
//    int32_t x, y;
//    dest_buf += dest_width * fill_area->y1; /* 移动到第一行 */
//
//    for(y = fill_area->y1; y <= fill_area->y2; y++) {
//        for(x = fill_area->x1; x <= fill_area->x2; x++) {
//            dest_buf[x] = color;
//        }
//        dest_buf+=dest_width;    /* 进入下一行 */
//    }
//}


#else /* 顶部未启用该文件 */

/* 该占位 typedef 仅用于消除 -Wpedantic 警告 */
typedef int keep_pedantic_happy;
#endif
