/* Prevent SDL from redefining main() to SDL_main() on Windows */
#define SDL_MAIN_HANDLED

/*
 * 本文件是 PC 模拟器的入口。
 * 运行逻辑（从上到下）：
 * 1) lv_init()：初始化 LVGL 核心。
 * 2) sdl_init()：初始化 lv_drivers 提供的 SDL 显示/输入驱动（内部会创建窗口、创建纹理、启动 SDL 事件定时器）。
 * 3) 配置并注册 LVGL 显示驱动：提供 draw buffer + flush 回调。
 * 4) 注册输入设备：鼠标/键盘/滚轮。
 * 5) app_init()：创建 UI（看板页面 + 定时刷新）。
 * 6) while(1) 主循环：
 *    - 用 SDL_GetTicks() 产生 tick，喂给 lv_tick_inc()。
 *    - 调用 lv_timer_handler() 执行 LVGL 的定时器/动画/刷新。
 *
 * 注意：这个工程的“数据”并不是随机直接喂给 UI，而是：
 * data_sim -> (模拟字节流写入 obuf) -> 解析 -> metrics -> dashboard 刷新。
 */

#include "lvgl.h"

/* lv_drivers v8.x: unified SDL driver */
#include "sdl/sdl.h"

#include <SDL.h>

#include "app/app.h"

int main(void)
{
    lv_init();

    /* Display */
    sdl_init();

    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf_1[MONITOR_HOR_RES * 120];
    static lv_color_t buf_2[MONITOR_HOR_RES * 120];

    /*
     * draw buffer 不是整屏 framebuffer（我们只给 800*120 的条带缓冲）。
     * LVGL 会把需要刷新的区域画到这个缓冲里，然后回调 flush_cb 把区域“贴”到屏幕上。
     */
    lv_disp_draw_buf_init(&draw_buf, buf_1, buf_2, MONITOR_HOR_RES * 120);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = sdl_display_flush;
    disp_drv.hor_res = MONITOR_HOR_RES;
    disp_drv.ver_res = MONITOR_VER_RES;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    /* Input devices */
    static lv_indev_drv_t mouse_drv;
    lv_indev_drv_init(&mouse_drv);
    mouse_drv.type = LV_INDEV_TYPE_POINTER;
    mouse_drv.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&mouse_drv);

    static lv_indev_drv_t kb_drv;
    lv_indev_drv_init(&kb_drv);
    kb_drv.type = LV_INDEV_TYPE_KEYPAD;
    kb_drv.read_cb = sdl_keyboard_read;
    lv_indev_drv_register(&kb_drv);

    static lv_indev_drv_t wheel_drv;
    lv_indev_drv_init(&wheel_drv);
    wheel_drv.type = LV_INDEV_TYPE_ENCODER;
    wheel_drv.read_cb = sdl_mousewheel_read;
    lv_indev_drv_register(&wheel_drv);

    app_init(disp);

    uint32_t last_ms = SDL_GetTicks();

    while (1) {
        const uint32_t now_ms = SDL_GetTicks();
        const uint32_t delta = now_ms - last_ms;
        if (delta) {
            lv_tick_inc(delta);
            last_ms = now_ms;
        }

        /* 执行 LVGL 内部定时器（包括 UI 的 lv_timer_create 刷新回调） */
        lv_timer_handler();
        SDL_Delay(5);
    }

    return 0;
}
