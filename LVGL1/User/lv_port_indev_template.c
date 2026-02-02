/**
 * @file lv_port_indev_templ.c
 * 触摸输入移植实现
 */

/* 复制本文件为 "lv_port_indev.c"，并将此值设为 "1" 启用内容 */
#if 1

/*********************
 *      头文件
 *********************/
#include "lv_port_indev_template.h"
#include "../../lvgl.h"
#include "./BSP/TOUCH/touch.h"

/*********************
 *      宏定义
 *********************/

/**********************
 *      类型定义
 **********************/

/**********************
 *  静态函数声明
 **********************/

static void touchpad_init(void);                                  /* 触摸初始化 */
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data); /* 读取触摸状态 */
static bool touchpad_is_pressed(void);                           /* 是否按下 */
static void touchpad_get_xy(lv_coord_t * x, lv_coord_t * y);      /* 读取坐标 */



/**********************
 *  静态变量
 **********************/
lv_indev_t * indev_touchpad;


/**********************
 *      宏
 **********************/

/**********************
 *   对外接口
 **********************/

void lv_port_indev_init(void)
{


    static lv_indev_drv_t indev_drv;

    /*------------------
     * 触摸
     * -----------------*/

    /* 初始化触摸硬件 */
    touchpad_init();

    /* 注册触摸输入设备到 LVGL */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    indev_touchpad = lv_indev_drv_register(&indev_drv);

    
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*------------------
 * 触摸
 * -----------------*/

/* 初始化触摸硬件 */
static void touchpad_init(void)
{
    tp_dev.init();/* 触摸芯片初始化 */
}

/* LVGL 周期调用读取触摸状态 */
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    /* 读取按下状态与坐标 */
    if(touchpad_is_pressed()) {
        touchpad_get_xy(&last_x, &last_y);
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

    /* 保存最后坐标 */
    data->point.x = last_x;
    data->point.y = last_y;
}

/* 是否检测到按下 */
static bool touchpad_is_pressed(void)
{
    tp_dev.scan(0);
	  if(tp_dev.sta & TP_PRES_DOWN)
		{
			return true;
		}

    return false;
}

/* 读取触摸坐标 */
static void touchpad_get_xy(lv_coord_t * x, lv_coord_t * y)
{
    /* 从触摸驱动读取坐标 */

    (*x) = tp_dev.x[0];
    (*y) = tp_dev.y[0];
}



#else /* 顶部未启用该文件 */

/* 该占位 typedef 仅用于消除 -Wpedantic 警告 */
typedef int keep_pedantic_happy;
#endif
