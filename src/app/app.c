#include "app.h"
#include "screens/dashboard.h"

/*
 * 【模块说明 / Module Description】
 * 模块名称: App Logic (应用逻辑模块)
 * 文件路径: d:\LVGL\LVGL1\User\app\app.c
 * 
 * 功能概述:
 * 1. 应用初始化 (app_init): 负责创建和加载主界面 (Dashboard)。
 * 2. 模拟数据逻辑 (Simulation): 提供一套用于无硬件调试的模拟数据生成器。
 *    - 包含: app_stop_sim() 用于在检测到真实数据时停止模拟。
 *    - 注意: 当前版本中，模拟定时器已被注释掉，以便显示真实串口数据。
 */

// ============================================================================
// 应用初始化函数
// 参数: disp - LVGL 显示驱动指针 (对于单屏系统通常为 NULL)
// ============================================================================
void app_init(lv_disp_t *disp)
{
    (void)disp; // 忽略未使用参数
    
    // 1. 创建 UI 场景
    // 调用 dashboard 模块创建主仪表盘对象
    lv_obj_t *scr = dashboard_create();
    
    // 2. 加载屏幕
    // 将创建好的界面加载到当前活动显示层
    lv_scr_load(scr);

    // 3. 配置模拟数据源 (调试用)
    // 说明: 之前通过定时器定期调用 dashboard_update 来演示界面动画。
    // 现在为了接收真实串口数据，必须禁用模拟器，否则模拟数据会覆盖串口数据，导致界面“乱跳”。
    /* 
     * 只有在没有真实数据源时才启动模拟定时器。
     * 这里的 lv_timer_create 原本会每 1000ms 调用一次 sim_timer_cb。
     * 现已被注释 (Disabled Simulation)。
     */
    // lv_timer_create(sim_timer_cb, 1000, NULL);
}

// 停止模拟 (外部调用接口)
void app_stop_sim(void)
{
    // 如果启用了 timer，可以在这里 lv_timer_del(timer_handle)
    // 目前逻辑是默认不启动，所以为空。
}

/*
 * 模拟数据生成器已移除（仅保留真实串口数据路径）。
 */

