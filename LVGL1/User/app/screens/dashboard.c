#include "dashboard.h"
#include "../app.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "ff.h"
#include "lvgl.h"

#define DASHBOARD_ENABLE_DEBUG 0
#define DASHBOARD_ENABLE_FONT_LOAD 1

/*
 * =========================================================================================
 * 模块说明
 * 模块名称: 仪表盘界面
 * 文件路径: d:\LVGL\LVGL1\User\app\screens\dashboard.c
 *
 * 功能概述:
 * 1. 左侧仪表盘：5 个同心圆弧显示工具面历史趋势（外圈最新、内圈最旧）。
 * 2. 右侧信息区：核心参数列表 + 实时解码数据表。
 * 3. 字体：优先从 NAND 加载中文字体，失败回退内置字体。
 *
 * 编码提示:
 * 本文件包含中文字符，建议以 UTF-8（无 BOM）保存，确保 Keil/ARMCC 正常显示。
 * =========================================================================================
 */

// NAND 字体运行时加载(优先)，失败则回退到内置字库
static const lv_font_t *g_font_cn_50 = &lv_font_montserrat_28;
static const lv_font_t *g_font_cn_70 = &lv_font_montserrat_28;
static const lv_font_t *g_font_cn_20 = &lv_font_montserrat_16;

static int font_has_lvgl_head(const char *path)
{
    FIL f;
    UINT br = 0;
    uint8_t head[8];

    if (!path) {
        return 0;
    }
    if (f_open(&f, path, FA_READ) != FR_OK) {
        return 0;
    }
    if (f_read(&f, head, sizeof(head), &br) != FR_OK || br < sizeof(head)) {
        f_close(&f);
        return 0;
    }
    f_close(&f);

    return (head[4] == 'h' && head[5] == 'e' && head[6] == 'a' && head[7] == 'd');
}

/*
 * 功能: 初始化运行时字体
 * 说明: 优先从 NAND 加载自定义字体，失败时回退到内置字体
 * 影响: 右侧数据表/弹窗等大字号文本的显示效果
 */
static void dashboard_font_init(void)
{
    FILINFO fno;
    if (f_stat("N:/font/my_font_70.bin", &fno) == FR_OK) {
        printf("[FONT] stat 70 size=%lu\r\n", (unsigned long)fno.fsize);
    } else {
        printf("[FONT] stat 70 FAIL\r\n");
    }

    if (font_has_lvgl_head("N:/font/my_font_70.bin")) {
        lv_font_t *f70 = lv_font_load("N:/font/my_font_70.bin");
        if (f70) {
            g_font_cn_70 = f70;
            printf("[FONT] Load 70 OK\r\n");
        } else {
            printf("[FONT] Load 70 FAIL, fallback to built-in\r\n");
        }
    } else {
        printf("[FONT] Load 70 FAIL, bad head\r\n");
    }

    if (f_stat("N:/font/my_font_52.bin", &fno) == FR_OK) {
        printf("[FONT] stat 52 size=%lu\r\n", (unsigned long)fno.fsize);
    } else {
        printf("[FONT] stat 52 FAIL\r\n");
    }

    if (font_has_lvgl_head("N:/font/my_font_52.bin")) {
        lv_font_t *f50 = lv_font_load("N:/font/my_font_52.bin");
        if (f50) {
            g_font_cn_50 = f50;
            printf("[FONT] Load 52 OK\r\n");
        } else {
            printf("[FONT] Load 52 FAIL, fallback to built-in\r\n");
        }
    } else {
        printf("[FONT] Load 52 FAIL, bad head\r\n");
    }
    if (f_stat("N:/font/my_font_20.bin", &fno) == FR_OK) {
        printf("[FONT] stat 20 size=%lu\r\n", (unsigned long)fno.fsize);
    } else {
        printf("[FONT] stat 20 FAIL\r\n");
    }

    if (font_has_lvgl_head("N:/font/my_font_20.bin")) {
        lv_font_t *f20 = lv_font_load("N:/font/my_font_20.bin");
        if (f20) {
            g_font_cn_20 = f20;
            printf("[FONT] Load 20 OK\r\n");
        } else {
            printf("[FONT] Load 20 FAIL, fallback to built-in\r\n");
        }
    } else {
        printf("[FONT] Load 20 FAIL, bad head\r\n");
    }
}

// ============================================================================
// 数据协议字段定义（参考 SQMWD_Tablet/mainwindow.h）
// 说明：对应串口协议 payload[5] 的类型码
// ============================================================================
typedef enum {
    /* 核心参数 */
    DT_INC   = 0x10, // 井斜：井眼倾角
    DT_AZI   = 0x11, // 方位：井眼相对北的方向
    DT_TF    = 0x12, // 工具面：通用工具面角度
    DT_GTF   = 0x13, // 重力工具面：基于重力的工具面
    DT_MTF   = 0x14, // 磁性工具面：基于磁场的工具面
    DT_DIP   = 0x15, // 磁倾角
    DT_TEMP  = 0x16, // 温度
    DT_VOLT  = 0x17, // 电池电压

    /* 扩展参数（如有） */
    DT_GRAV_TOTAL = 0x1F, // 总重力
    DT_MAG_TOTAL  = 0x20, // 总磁场
} probe_data_type_t;

// ---------------------------------------------------------
// UI 结构体定义
// 保存界面中需要动态更新的 LVGL 对象指针
// ---------------------------------------------------------
typedef struct {
    lv_obj_t *root;
    // 左侧：仪表盘（5 个同心圆弧）
    // arcs[0] 为内圈（历史），arcs[4] 为外圈（最新）
    lv_obj_t *arcs[5]; 

    // 右侧：数值显示标签（列表形式）
    lv_obj_t *label_inc;      // 井斜数值标签
    lv_obj_t *label_azi;      // 方位数值标签
    lv_obj_t *label_tf;       // 工具面数值标签
    lv_obj_t *label_tf_title; // 工具面标题标签
    lv_obj_t *label_pump;     // 泵压数值标签
    lv_obj_t *label_pump_status; // 开关泵时间标签
    lv_obj_t *label_pump_status_title; // 开关泵标题标签

    lv_obj_t *row_inc;
    lv_obj_t *row_azi;
    lv_obj_t *row_tf;
    lv_obj_t *row_pump;
    lv_obj_t *row_pump_status;

    // 列表/日志容器
    lv_obj_t *table_cont;     // 表格外层滚动容器
    lv_obj_t *table_decode;   // 解码数据表
    
    // 顶部状态栏
    lv_obj_t *label_comm_info; // 通讯信息标签（如“COM1 连接”）

    // 解码表下方通信状态
    lv_obj_t *label_comm_status;

    // 消息区域（固定右下角）
    lv_obj_t *msg_cont;
    lv_obj_t *msg_label;
    lv_timer_t *msg_timer;

    // 调试小部件
    lv_obj_t *dbg_cont;
    lv_obj_t *dbg_line1;
    lv_obj_t *dbg_line2;
    lv_obj_t *dbg_line3;
    lv_obj_t *dbg_line4;
    lv_obj_t *dbg_line5;

} dashboard_ui_t;

// 全局 UI 实例，用于在 update 函数中访问对象
static dashboard_ui_t g_ui;
static const uint32_t k_decode_rows = 9;//参数解码表行数
static char g_msg_text[256];
static const uint16_t k_msg_line_chars = 18;
static uint8_t g_msg_active = 0;
static uint8_t g_msg_persistent = 0;
static int g_pump_status_last = -1;
static uint32_t g_pump_status_elapsed_sec = 0;
static lv_timer_t *g_pump_status_timer = NULL;
static uint8_t g_pump_status_time_enabled = 0;

static void update_pump_status_time(void)
{
    if (!g_ui.label_pump_status) {
        return;
    }

    uint32_t sec = g_pump_status_elapsed_sec;
    uint32_t hh = (sec / 3600U) % 24U;
    uint32_t mm = (sec / 60U) % 60U;
    uint32_t ss = sec % 60U;

    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
             (unsigned long)hh,
             (unsigned long)mm,
             (unsigned long)ss);
    if (!g_pump_status_time_enabled) {
        lv_label_set_text(g_ui.label_pump_status, "00:00:00");
        return;
    }
    lv_label_set_text(g_ui.label_pump_status, buf);
    lv_obj_set_style_text_color(g_ui.label_pump_status, lv_color_black(), 0);
}

static void pump_status_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (g_pump_status_time_enabled) {
        g_pump_status_elapsed_sec++;
    }
    update_pump_status_time();
}

static void set_row_highlight(lv_obj_t *row, uint8_t on)
{
    if (!row) {
        return;
    }

    if (on) {
        lv_obj_set_style_bg_opa(row, LV_OPA_40, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_radius(row, 0, 0);
    } else {
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    }
}
static void msg_touch_close_cb(lv_event_t *e);

static size_t utf8_char_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static void build_two_line_msg(const char *text)
{
    size_t out = 0;
    int line = 0;
    uint16_t chars = 0;
    const char *p = text;

    while (*p && line < 2) {
        if (*p == '\n') {
            if (line == 0 && out + 1 < sizeof(g_msg_text)) {
                g_msg_text[out++] = '\n';
                line++;
                chars = 0;
            }
            p++;
            continue;
        }

        if (chars >= k_msg_line_chars) {
            if (line == 0 && out + 1 < sizeof(g_msg_text)) {
                g_msg_text[out++] = '\n';
                line++;
                chars = 0;
                continue;
            }
            break;
        }

        size_t clen = utf8_char_len((unsigned char)*p);
        if (out + clen >= sizeof(g_msg_text)) {
            break;
        }
        for (size_t i = 0; i < clen; i++) {
            g_msg_text[out++] = *p++;
        }
        chars++;
    }

    g_msg_text[out] = '\0';
}

/* 使用整数拼接小数，避免浮点 printf 在嵌入式环境显示异常
 * 说明：部分工具链未开启浮点 printf，导致小数点丢失/数值放大
 * 处理：把 float 按比例放大为整数，再拼出字符串
 */
static void format_fixed(char *buf, size_t cap, float v, int decimals)
{
    int scale = (decimals == 1) ? 10 : 100;
    int vi = (int)lrintf(v * (float)scale);
    int sign = 0;
    if (vi < 0) {
        sign = 1;
        vi = -vi;
    }
    int ip = vi / scale;
    int fp = vi % scale;

    if (decimals == 1) {
        snprintf(buf, cap, sign ? "-%d.%d" : "%d.%d", ip, fp);
    } else {
        snprintf(buf, cap, sign ? "-%d.%02d" : "%d.%02d", ip, fp);
    }
}

/* 生成“运行时刻”字符串（HH:MM:SS）
 * 说明：无 RTC 时用系统运行时间代替当前时间
 */
static void format_uptime(char *buf, size_t cap)
{
    uint32_t sec = lv_tick_get() / 1000U;
    uint32_t hh = (sec / 3600U) % 24U;
    uint32_t mm = (sec / 60U) % 60U;
    uint32_t ss = sec % 60U;
    snprintf(buf, cap, "%02lu:%02lu:%02lu",
             (unsigned long)hh,
             (unsigned long)mm,
             (unsigned long)ss);
}

// ---------------------------------------------------------
// 辅助函数：创建数据行（标题 + 数值）
// 目的：把“标题 + 数值”组成一行，供右侧数据列表复用
// 参数:
//   parent: 父对象（容器）
//   title:  左侧显示的标题文本（如“井斜”）
//   val_label_out: 输出参数，返回右侧数值标签指针用于动态刷新
// ---------------------------------------------------------
/*
 * 功能: 创建右侧数据表的一行(标题 + 数值)
 * 说明: 返回行容器指针用于后续高亮；输出标题/数值标签指针用于动态更新
 */
static lv_obj_t *create_data_row(lv_obj_t *parent, const char *title, lv_obj_t **val_label_out, lv_obj_t **title_label_out)
{
    // 创建行容器
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_width(cont, LV_PCT(100));     // 宽度占满父容器
    lv_obj_set_height(cont, LV_SIZE_CONTENT); // 高度随内容自适应
    lv_obj_set_style_bg_opa(cont, 0, 0);      // 背景透明
    lv_obj_set_style_border_width(cont, 0, 0); // 无边框
    
    // 只在底部加一条分割线 (底部分隔)
    lv_obj_set_style_border_width(cont, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(cont, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont, lv_color_hex(0xE0E0E0), LV_PART_MAIN); // 浅灰色分割线
    lv_obj_set_style_pad_ver(cont, 6, 0); // 设置上下内边距
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE); // 禁止该行滚动

    // 使用 Flex 布局：左右两端对齐
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // [左侧] 标题标签
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, g_font_cn_50, 0); // 使用大号中文字体
    lv_obj_set_style_text_color(label, lv_color_black(), 0); // 黑色文本
    lv_obj_set_style_min_width(label, 60, 0); // 最小宽度保证对齐
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP); // 禁止文字竖向换行

    if (title_label_out) {
        *title_label_out = label;
    }

    // [右侧] 数值标签：只显示数字，避免中文字体缺字造成异常
    lv_obj_t *val = lv_label_create(cont);
    lv_label_set_text(val, "0.00"); // 默认初始值
    lv_obj_set_style_text_font(val, g_font_cn_50, 0); // 使用 font50
    lv_obj_set_style_text_color(val, lv_color_hex(0x002FA7), 0); // 蓝色 (0,47,167)
    // 让数值稍微靠左一点避免贴边
    lv_obj_set_style_pad_right(val, 5, 0);

    // 将数值标签指针赋值给输出参数
    *val_label_out = val;
    return cont;
}

// ---------------------------------------------------------
// 仪表盘创建：5 个同心圆弧
// 功能：在屏幕左侧创建圆弧仪表盘，用于可视化工具面历史趋势
// 说明：外圈代表最新数据，内圈代表历史数据
// ---------------------------------------------------------
/*
 * 功能: 创建工具面方位图(5 个同心圆环 + 角度刻度)
 * 说明: 圆环颜色由 GTF/MTF 类型决定，外圈表示最新数据
 */
static lv_obj_t *create_toolface_dial(lv_obj_t *parent)
{
    // 创建仪表盘容器
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 720, 720); // 进一步放大仪表盘容器
    lv_obj_set_style_bg_opa(cont, 0, 0); // 透明背景
    lv_obj_set_style_border_width(cont, 0, 0); // 无边框
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 10); // 下移少量避免外圈刻度被遮盖

    int max_r = 340; /// 最外圈半径
    int ring_w = 35; /// 圆环宽度
    int ring_gap = 20;/// 圆环间距
    
    // 循环创建5个圆环 (i=0为最内圈, i=4为最外圈)
    for (int i = 0; i < 5; i++) {
        // 计算当前圆环的半径: 外圈大，内圈小
        int current_r = max_r - (4 - i) * (ring_w + ring_gap);
        int size = current_r * 2;

        // 创建 Arc 对象
        lv_obj_t *arc = lv_arc_create(cont);
        lv_obj_set_size(arc, size, size);
        lv_arc_set_rotation(arc, 270); // 旋转起点到 12 点钟方向（LVGL 默认 0 度在 3 点钟）
        lv_arc_set_bg_angles(arc, 0, 360); // 背景圆弧占满一圈
        lv_arc_set_range(arc, 0, 360); // 设置数值范围为 0-360 度
        lv_arc_set_value(arc, 0); // 初始值 0
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL); 

        lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0); // 也是居中叠加
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB); // 移除旋钮，作为纯显示仪表
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE); // 禁止用户点击交互

        // 样式 - 背景 (未填充部分)
        lv_obj_set_style_arc_width(arc, ring_w, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0xE0E0E0), LV_PART_MAIN); // 浅灰色底
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN); // 平头端点

        // 样式 - 指示器 (填充部分)
        lv_obj_set_style_arc_width(arc, ring_w, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0x002FA7), LV_PART_INDICATOR); // 默认蓝色
        
        // 透明度渐变：内圈更淡（历史久）→ 外圈更深（最新）
        int opacities[] = {80, 120, 160, 210, 255};
        lv_obj_set_style_arc_opa(arc, (lv_opa_t)opacities[i], LV_PART_INDICATOR);
        
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR); 

        // 保存对象指针以便后续 update 使用
        g_ui.arcs[i] = arc;
    }

    // 绘制四周角度刻度标签（0、30、60 ...）
    int label_r = max_r + 6; // 标签半径内收，避免遮挡
    for (int deg = 0; deg < 360; deg += 30) {
        lv_obj_t *lbl = lv_label_create(cont);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", deg);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0); // 放大刻度数字
        lv_obj_set_style_text_color(lbl, lv_color_black(), 0);

        // 计算标签位置（极坐标转直角坐标）
        double rad = (double)deg * 3.1415926535 / 180.0;
        int x_offset = (int)(label_r * sin(rad));
        int y_offset = (int)(-label_r * cos(rad)); // Y轴向下为正，cos(0)在上方需要负号
        lv_obj_align(lbl, LV_ALIGN_CENTER, x_offset, y_offset);
    }
    return cont;
}

// ---------------------------------------------------------
// UI 主构建函数
// 功能：初始化整个仪表盘界面，创建所有 LVGL 对象
// ---------------------------------------------------------
/*
 * 功能: 创建主界面
 * 说明: 构建左侧方位图 + 右侧数据表/解码表 + 状态栏/弹窗等
 */
lv_obj_t *dashboard_create(void)
{
#if DASHBOARD_ENABLE_FONT_LOAD
    dashboard_font_init();
#endif
    // 1. 创建根页面
    lv_obj_t *scr = lv_obj_create(NULL);
    g_ui.root = scr;
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0); // 设置背景为白色
    lv_obj_set_style_pad_all(scr, 5, 0); // 全局 5px 内边距
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW); // 设置为水平布局（左右分栏）
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE); // 根页面禁止滚动

    /*
     * 左上角 Logo（来自 NAND 文件系统）
     * - 资源路径: N:/image_type/shiqi_logo2.bin
     * - 原图尺寸: 186x62 (RGB565/或带 Alpha)
     */
    /* 为透明图片提供白色底板，避免透明区域显示为黑色 */
    lv_obj_t *logo_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(logo_bg, 93, 31);
    lv_obj_set_style_bg_color(logo_bg, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(logo_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(logo_bg, 0, 0);
    lv_obj_clear_flag(logo_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(logo_bg, LV_ALIGN_TOP_LEFT, 5, 5);

    lv_obj_t *logo = lv_img_create(logo_bg);
    lv_img_set_src(logo, "N:/image_type/shiqi_logo2.bin");
    /* 原始尺寸 186x62，直接显示 */
    lv_img_set_zoom(logo, 256);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, 0);

    // 顶部左侧信息区（SQMWD + 串口状态）
    lv_obj_t *info_cont = lv_obj_create(lv_layer_top());
    lv_obj_set_size(info_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info_cont, 0, 0);
    lv_obj_set_style_border_width(info_cont, 0, 0);
    lv_obj_set_style_pad_all(info_cont, 0, 0);
    lv_obj_clear_flag(info_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    /* 放在 Logo 下方，避免遮挡 */
    lv_obj_align(info_cont, LV_ALIGN_TOP_LEFT, 5, 5 + 31 + 4);

    /* 左上角标题 */

    lv_obj_t *lbl_title = lv_label_create(info_cont);
    lv_label_set_text(lbl_title, "SQMWD");
    lv_obj_set_style_text_font(lbl_title, g_font_cn_20, 0);

    g_ui.label_comm_info = lv_label_create(info_cont);
    lv_label_set_text(g_ui.label_comm_info, "COM.. --");
    lv_obj_set_style_text_font(g_ui.label_comm_info, g_font_cn_20, 0);
    lv_obj_set_style_text_color(g_ui.label_comm_info, lv_color_hex(0x666666), 0);

    // 2. 左侧：仪表盘区域
    lv_obj_t *left_panel = lv_obj_create(scr);
    lv_obj_set_size(left_panel, LV_PCT(60), LV_PCT(100)); // 宽度占 60%，高度占满
    lv_obj_set_style_border_width(left_panel, 0, 0);      // 无边框
    lv_obj_clear_flag(left_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(left_panel, LV_FLEX_FLOW_COLUMN); // 内部垂直居中
    lv_obj_set_flex_align(left_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 调用辅助函数创建同心圆仪表盘
    create_toolface_dial(left_panel);

    // 3. 右侧：数据与列表区域
    lv_obj_t *right_panel = lv_obj_create(scr);
    lv_obj_set_size(right_panel, LV_PCT(40), LV_PCT(100)); // 宽度占 40%
    lv_obj_set_style_border_width(right_panel, 0, 0); // 无边框
    lv_obj_set_style_pad_right(right_panel, 0, 0); // 贴右边
    lv_obj_set_style_pad_left(right_panel, 0, 0);
    lv_obj_set_style_pad_top(right_panel, 0, 0);
    lv_obj_set_style_pad_bottom(right_panel, 0, 0);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN); // 内部垂直布局 (上:列表, 下:表格)
    lv_obj_set_flex_align(right_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
    
    // 3.1 数据列表区：显示实时值
    lv_obj_t *data_list_cont = lv_obj_create(right_panel);
    lv_obj_set_width(data_list_cont, LV_PCT(96));
    lv_obj_set_height(data_list_cont, LV_SIZE_CONTENT); // 高度自适应内容
    lv_obj_set_style_border_width(data_list_cont, 1, 0); // 外框细线
    lv_obj_set_style_border_color(data_list_cont, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(data_list_cont, 0, 0);
    lv_obj_set_style_pad_all(data_list_cont, 6, 0);
    lv_obj_clear_flag(data_list_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(data_list_cont, LV_FLEX_FLOW_COLUMN); // 垂直排列

    // 3.1.2 创建各项数据行
    // 使用 create_data_row 辅助函数批量创建
    g_ui.row_inc = create_data_row(data_list_cont, "井  斜", &g_ui.label_inc, NULL); 
    g_ui.row_azi = create_data_row(data_list_cont, "方  位", &g_ui.label_azi, NULL); 
    g_ui.row_tf = create_data_row(data_list_cont, "工具面 TF", &g_ui.label_tf, &g_ui.label_tf_title); 
    g_ui.row_pump = create_data_row(data_list_cont, "泵压 MPa", &g_ui.label_pump, NULL); 
    g_ui.row_pump_status = create_data_row(data_list_cont, "开关泵", &g_ui.label_pump_status, &g_ui.label_pump_status_title);
    
    // 强制 "状态" 值使用支持中文的字体 (因为要显示 "开泵"/"关泵")
    lv_obj_set_style_text_font(g_ui.label_pump_status, g_font_cn_50, 0);

    // 3.2 解码数据表格：滚动显示历史记录
    // 说明：表头与数据表分离，保证表头固定，数据可滚动
    // 3.2.1 固定表头（使用 1 行表格对齐，不随数据滚动）
    lv_obj_t *table_header = lv_table_create(right_panel);
    lv_obj_set_width(table_header, LV_PCT(96));
    lv_obj_set_height(table_header, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(table_header, 0, 0);
    lv_obj_set_style_border_width(table_header, 0, 0);
    lv_obj_set_style_radius(table_header, 0, 0);
    lv_obj_set_style_bg_opa(table_header, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(table_header, LV_OBJ_FLAG_CLICKABLE);
    
    // 设置表头中文字体
    lv_obj_set_style_text_font(table_header, g_font_cn_20, LV_PART_ITEMS);
    // 配置列宽 (共3列：参数 / 解码值 / 时间)
    lv_table_set_col_cnt(table_header, 3);
    lv_table_set_col_width(table_header, 0, 120); // 参数
    lv_table_set_col_width(table_header, 1, 120); // 解码值
    lv_table_set_col_width(table_header, 2, 120); // 时间
    lv_obj_set_style_pad_all(table_header, 2, LV_PART_ITEMS);
    // 填充表头文本
    lv_table_set_cell_value(table_header, 0, 0, "参数");
    lv_table_set_cell_value(table_header, 0, 1, "解码值");
    lv_table_set_cell_value(table_header, 0, 2, "时间");

    // 3.2.2 数据列表容器 (可滚动)
    g_ui.table_cont = lv_obj_create(right_panel);
    lv_obj_set_width(g_ui.table_cont, LV_PCT(96));
    lv_obj_set_flex_grow(g_ui.table_cont, 1); // 占据剩余所有垂直空间
    lv_obj_set_style_pad_all(g_ui.table_cont, 0, 0);
    lv_obj_set_style_border_width(g_ui.table_cont, 1, 0);
    lv_obj_set_style_border_color(g_ui.table_cont, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(g_ui.table_cont, 0, 0);
    lv_obj_clear_flag(g_ui.table_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_ui.table_cont, LV_SCROLLBAR_MODE_OFF);

    // 3.2.3 实际数据表格
    // 说明：使用中文字体展示参数名称；数值显示走 format_fixed，避免小数点异常
    g_ui.table_decode = lv_table_create(g_ui.table_cont);
    lv_obj_set_width(g_ui.table_decode, LV_PCT(100)); 
    lv_obj_clear_flag(g_ui.table_decode, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(g_ui.table_decode, LV_SCROLLBAR_MODE_OFF);
    
    // 设置表格内容字体
    lv_obj_set_style_text_font(g_ui.table_decode, g_font_cn_20, LV_PART_ITEMS);

    // 为同步头行准备高亮样式 (使用 USER_1 状态)
    lv_obj_set_style_bg_opa(g_ui.table_decode, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_USER_1);
    lv_obj_set_style_bg_color(g_ui.table_decode, lv_color_hex(0x00FF00), LV_PART_ITEMS | LV_STATE_USER_1);
    lv_obj_set_style_bg_opa(g_ui.table_decode, LV_OPA_40, LV_PART_ITEMS | LV_STATE_USER_1);
    lv_obj_set_style_radius(g_ui.table_decode, 0, LV_PART_ITEMS | LV_STATE_USER_1);
    lv_obj_set_style_text_color(g_ui.table_decode, lv_color_black(), LV_PART_ITEMS | LV_STATE_USER_1);

    // 配置列宽 (必须与表头一致)
    lv_table_set_col_cnt(g_ui.table_decode, 3);
    lv_table_set_col_width(g_ui.table_decode, 0, 120); 
    lv_table_set_col_width(g_ui.table_decode, 1, 100); 
    lv_table_set_col_width(g_ui.table_decode, 2, 120); 
    lv_obj_set_style_pad_all(g_ui.table_decode, 2, LV_PART_ITEMS);  

    lv_table_set_row_cnt(g_ui.table_decode, k_decode_rows);
    for (uint32_t r = 0; r < k_decode_rows; r++) {
        lv_table_set_cell_value(g_ui.table_decode, r, 0, "");
        lv_table_set_cell_value(g_ui.table_decode, r, 1, "");
        lv_table_set_cell_value(g_ui.table_decode, r, 2, "");
    }

    // 3.3 解码表下方通信状态条
    {
        lv_obj_t *comm_cont = lv_obj_create(right_panel);
        lv_obj_set_width(comm_cont, LV_PCT(96));
        lv_obj_set_height(comm_cont, 24);
        lv_obj_set_style_border_width(comm_cont, 1, 0);
        lv_obj_set_style_border_color(comm_cont, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_radius(comm_cont, 0, 0);
        lv_obj_set_style_pad_all(comm_cont, 2, 0);
        lv_obj_set_style_bg_opa(comm_cont, 0, 0);
        lv_obj_clear_flag(comm_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        g_ui.label_comm_status = lv_label_create(comm_cont);
        lv_label_set_text(g_ui.label_comm_status, "通讯超时");
        lv_obj_set_style_text_font(g_ui.label_comm_status, g_font_cn_20, 0);
        lv_obj_set_style_text_color(g_ui.label_comm_status, lv_color_hex(0xB22222), 0);
        lv_obj_align(g_ui.label_comm_status, LV_ALIGN_LEFT_MID, 4, 0);
    }

    // 4. 固定消息区域（界面中间，置于顶层不遮挡主布局）
    g_ui.msg_cont = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_ui.msg_cont, LV_PCT(80), LV_PCT(60));
    lv_obj_set_style_bg_color(g_ui.msg_cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_ui.msg_cont, LV_OPA_40, 0);
    lv_obj_set_style_anim_time(g_ui.msg_cont, 0, 0);
    lv_obj_set_style_border_width(g_ui.msg_cont, 1, 0);
    lv_obj_set_style_border_color(g_ui.msg_cont, lv_color_hex(0x666666), 0);
    lv_obj_set_style_pad_all(g_ui.msg_cont, 8, 0);
    lv_obj_set_style_radius(g_ui.msg_cont, 8, 0);
    lv_obj_set_flex_flow(g_ui.msg_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_ui.msg_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(g_ui.msg_cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g_ui.msg_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_ui.msg_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_ui.msg_cont, msg_touch_close_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_ui.msg_cont, msg_touch_close_cb, LV_EVENT_CLICKED, NULL);

    g_ui.msg_label = lv_label_create(g_ui.msg_cont);
    lv_label_set_long_mode(g_ui.msg_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_ui.msg_label, LV_PCT(100));
    lv_label_set_text(g_ui.msg_label, "");
    lv_obj_set_style_text_color(g_ui.msg_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_ui.msg_label, g_font_cn_70, 0);
    lv_obj_set_style_text_align(g_ui.msg_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(g_ui.msg_label, 0, 0);
    lv_obj_align(g_ui.msg_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g_ui.msg_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_ui.msg_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_ui.msg_label, msg_touch_close_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_ui.msg_label, msg_touch_close_cb, LV_EVENT_CLICKED, NULL);

    if (!g_pump_status_timer) {
        g_pump_status_timer = lv_timer_create(pump_status_timer_cb, 1000, NULL);
    }

#if DASHBOARD_ENABLE_DEBUG
    // 5. 调试小部件（悬浮，不影响主布局）
    g_ui.dbg_cont = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_ui.dbg_cont, 360, 130);
    lv_obj_set_style_bg_color(g_ui.dbg_cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_ui.dbg_cont, LV_OPA_50, 0);
    lv_obj_set_style_border_width(g_ui.dbg_cont, 1, 0);
    lv_obj_set_style_border_color(g_ui.dbg_cont, lv_color_hex(0x999999), 0);
    lv_obj_set_style_pad_all(g_ui.dbg_cont, 6, 0);
    lv_obj_set_style_radius(g_ui.dbg_cont, 6, 0);
    lv_obj_align(g_ui.dbg_cont, LV_ALIGN_TOP_LEFT, 6, 6);

    g_ui.dbg_line1 = lv_label_create(g_ui.dbg_cont);
    g_ui.dbg_line2 = lv_label_create(g_ui.dbg_cont);
    g_ui.dbg_line3 = lv_label_create(g_ui.dbg_cont);
    g_ui.dbg_line4 = lv_label_create(g_ui.dbg_cont);
    g_ui.dbg_line5 = lv_label_create(g_ui.dbg_cont);

    lv_obj_set_style_text_font(g_ui.dbg_line1, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(g_ui.dbg_line2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(g_ui.dbg_line3, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(g_ui.dbg_line4, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_font(g_ui.dbg_line5, &lv_font_montserrat_12, 0);

    lv_label_set_text(g_ui.dbg_line1, "DBG: init");
    lv_label_set_text(g_ui.dbg_line2, "RX: 0");
    lv_label_set_text(g_ui.dbg_line3, "OK: 0  BAD: 0");
    lv_label_set_text(g_ui.dbg_line4, "SUB: --");
    lv_label_set_text(g_ui.dbg_line5, "LAST: --");

    lv_obj_align(g_ui.dbg_line1, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(g_ui.dbg_line2, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_obj_align(g_ui.dbg_line3, LV_ALIGN_TOP_LEFT, 0, 44);
    lv_obj_align(g_ui.dbg_line4, LV_ALIGN_TOP_LEFT, 0, 66);
    lv_obj_align(g_ui.dbg_line5, LV_ALIGN_TOP_LEFT, 0, 88);
#endif
    
    return scr;
}

// ---------------------------------------------------------
// 界面更新函数
// 功能: 被上层 (如串口解析逻辑) 调用，将最新业务数据刷新到界面
// 参数: 
//   data: 最新的设备状态数据结构体指针
// ---------------------------------------------------------
/*
 * 功能: 刷新界面数据
 * 说明: 根据业务数据更新数值、颜色、高亮、工具面环形显示
 */
void dashboard_update(const plant_metrics_t *data)
{
    char buf[32];

    // 1. 更新顶部实时数据列表
    // ------------------------------------------------
    // 更新井斜
    format_fixed(buf, sizeof(buf), data->inclination, 2);
    lv_label_set_text(g_ui.label_inc, buf);
    
    // 更新方位
    format_fixed(buf, sizeof(buf), data->azimuth, 2);
    lv_label_set_text(g_ui.label_azi, buf);
    
    // 更新工具面
    format_fixed(buf, sizeof(buf), data->toolface, 1);
    lv_label_set_text(g_ui.label_tf, buf);
    if (g_ui.label_tf_title) {
        if (data->tf_type == 0x14) {
            lv_label_set_text(g_ui.label_tf_title, "MTF");
        } else if (data->tf_type == 0x13) {
            lv_label_set_text(g_ui.label_tf_title, "GTF");
        } else {
            lv_label_set_text(g_ui.label_tf_title, "TF");
        }
    }
    
    // 更新泵压
    format_fixed(buf, sizeof(buf), data->pump_pressure, 1);
    lv_label_set_text(g_ui.label_pump, buf);

    // 更新开关泵状态 + 进入时间
    if (!data->pump_pressure_valid) {
        g_pump_status_time_enabled = 0;
        g_pump_status_elapsed_sec = 0;
        if (g_ui.label_pump_status_title) {
            lv_label_set_text(g_ui.label_pump_status_title, "");
        }
        lv_label_set_text(g_ui.label_pump_status, "00:00:00");
    } else if (data->pump_status != 0 && data->pump_status != 1) {
        g_pump_status_time_enabled = 0;
        g_pump_status_last = -1;
        if (g_ui.label_pump_status_title) {
            lv_label_set_text(g_ui.label_pump_status_title, "");
        }
        lv_label_set_text(g_ui.label_pump_status, "00:00:00");
    } else {
        g_pump_status_time_enabled = 1;
        if (g_pump_status_last != data->pump_status) {
            g_pump_status_last = data->pump_status;
            g_pump_status_elapsed_sec = 0;
        }

        if (g_ui.label_pump_status_title) {
            lv_label_set_text(g_ui.label_pump_status_title, data->pump_status ? "开泵" : "关泵");
            lv_obj_set_style_text_color(g_ui.label_pump_status_title, lv_color_black(), 0);
        }

        update_pump_status_time();
    }

    // 更新高亮行（最近更新的数据）
    set_row_highlight(g_ui.row_inc, data->last_update_id == UPDATE_INC);
    set_row_highlight(g_ui.row_azi, data->last_update_id == UPDATE_AZI);
    set_row_highlight(g_ui.row_tf, data->last_update_id == UPDATE_TF);
    set_row_highlight(g_ui.row_pump, data->last_update_id == UPDATE_PUMP);

    // 更新通讯状态（连接/断开状态提示）
    const char *com_id = "COM?";
    if (strncmp(data->port_name, "UART1", 5) == 0) {
        com_id = "COM1";
    } else if (strncmp(data->port_name, "UART2", 5) == 0) {
        com_id = "COM2";
    } else if (strncmp(data->port_name, "UART3", 5) == 0) {
        com_id = "COM3";
    }

    if (data->port_connected) {
        // 例如：“COM2 通信中”
        snprintf(buf, sizeof(buf), "%s 通信中", com_id);
        lv_obj_set_style_text_color(g_ui.label_comm_info, lv_color_hex(0x00C800), 0); // 绿色 (0,200,0)
    } else {
        // 例如：“COM2 无信号”
        snprintf(buf, sizeof(buf), "%s 无信号", com_id);
        lv_obj_set_style_text_color(g_ui.label_comm_info, lv_color_hex(0xB22222), 0); // 火砖红
    }
    lv_label_set_text(g_ui.label_comm_info, buf);

    // 更新解码表下方通信状态（10秒内有数据增长）
    if (g_ui.label_comm_status) {
        if (data->comm_alive && data->port_connected) {
            lv_label_set_text(g_ui.label_comm_status, "通信正常");
            lv_obj_set_style_text_color(g_ui.label_comm_status, lv_color_hex(0x00C800), 0);
        } else {
            lv_label_set_text(g_ui.label_comm_status, "通信超时");
            lv_obj_set_style_text_color(g_ui.label_comm_status, lv_color_hex(0xB22222), 0);
        }
    }


    // 2. 更新仪表盘
    // ------------------------------------------------
    // 遍历5个圆环，将历史队列中的数据映射上去
    for (int i = 0; i < 5; i++) {
        // arcs[i] 对应: 0=内圈(旧数据), 4=外圈(新数据)
        // history[i] 对应: 0=最旧时刻, 4=最新时刻
        // data->toolface_history 是一个 FIFO 队列，由上层维护移位
        float val = data->toolface_history[i];

        // 工具面颜色随类型切换：GTF/MTF 使用蓝/紫区分
        lv_color_t tf_color = lv_color_hex(0x002FA7); // 默认蓝色
        if (data->toolface_type_history[i] == 0x14) {
            tf_color = lv_color_hex(0x800080); // 磁性工具面：紫色 (128,0,128)
        } else if (data->toolface_type_history[i] == 0x13) {
            tf_color = lv_color_hex(0x002FA7); // 重力工具面：蓝色
        }
        lv_obj_set_style_arc_color(g_ui.arcs[i], tf_color, LV_PART_INDICATOR);

        // 角度严格一一对应：0~360，超范围截断
        int32_t angle = (int32_t)lrintf(val);
        /* 角度范围保护，避免异常值导致弧度显示错误 */
        if (angle < 0) {
            angle = 0;
        } else if (angle > 360) {
            angle = 360;
        }
        lv_arc_set_angles(g_ui.arcs[i], 0, angle);
    }

    // 3. 解码表改由 dashboard_append_decode_row() 驱动
}

// ---------------------------------------------------------
// 追加一条解码数据行 (由协议解析层调用)
// ---------------------------------------------------------
/*
 * 功能: 追加一条“参数解码值”记录(数值)
 * 说明: 采用滚动上移方式，最后一行写入新记录；支持高亮
 */
void dashboard_append_decode_row(const char *name, float value, int highlight)
{
    if (!g_ui.table_decode || !name) {
        return;
    }

    /* 旧数据上移一行，最后一行写入新数据 */
    for (uint32_t r = 0; r + 1 < k_decode_rows; r++) {
        for (int col = 0; col < 3; col++) {
            const char *v = lv_table_get_cell_value(g_ui.table_decode, r + 1, col);
            lv_table_set_cell_value(g_ui.table_decode, r, col, v ? v : "");

            if (lv_table_has_cell_ctrl(g_ui.table_decode, r + 1, col, LV_TABLE_CELL_CTRL_CUSTOM_1)) {
                lv_table_add_cell_ctrl(g_ui.table_decode, r, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
            } else {
                lv_table_clear_cell_ctrl(g_ui.table_decode, r, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
            }
        }
    }

    uint32_t new_row = k_decode_rows - 1;
    char val_str[24];
    char tbuf[16];
    format_fixed(val_str, sizeof(val_str), value, 2);
    format_uptime(tbuf, sizeof(tbuf));
    if (strcmp(name, "重力工具面") == 0) {
        lv_table_set_cell_value(g_ui.table_decode, new_row, 0, "GTF");
    } else if (strcmp(name, "磁性工具面") == 0) {
        lv_table_set_cell_value(g_ui.table_decode, new_row, 0, "MTF");
    } else {
        lv_table_set_cell_value(g_ui.table_decode, new_row, 0, name);
    }
    lv_table_set_cell_value(g_ui.table_decode, new_row, 1, val_str);
    lv_table_set_cell_value(g_ui.table_decode, new_row, 2, tbuf);

    /* 清除上一轮可能遗留的高亮标记 */
    for (int col = 0; col < 3; col++) {
        lv_table_clear_cell_ctrl(g_ui.table_decode, new_row, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
    }

    /* 需要高亮时再加标记（同步头等场景） */
    if (highlight) {
        for (int col = 0; col < 3; col++) {
            lv_table_add_cell_ctrl(g_ui.table_decode, new_row, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
        }
    }
}

// ---------------------------------------------------------
// 追加一条解码数据行（字符串值）
// 用途：兼容 Tablet 中“序列/QID”等非数值显示
// ---------------------------------------------------------
/*
 * 功能: 追加一条“参数解码值”记录(字符串)
 * 说明: 用于显示非数值字段(如序列号)，与数值版逻辑一致
 */
void dashboard_append_decode_text_row(const char *name, const char *value_text, int highlight)
{
    if (!g_ui.table_decode || !name || !value_text) {
        return;
    }

    /* 旧数据上移一行，最后一行写入新数据 */
    for (uint32_t r = 0; r + 1 < k_decode_rows; r++) {
        for (int col = 0; col < 3; col++) {
            const char *v = lv_table_get_cell_value(g_ui.table_decode, r + 1, col);
            lv_table_set_cell_value(g_ui.table_decode, r, col, v ? v : "");

            if (lv_table_has_cell_ctrl(g_ui.table_decode, r + 1, col, LV_TABLE_CELL_CTRL_CUSTOM_1)) {
                lv_table_add_cell_ctrl(g_ui.table_decode, r, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
            } else {
                lv_table_clear_cell_ctrl(g_ui.table_decode, r, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
            }
        }
    }

    uint32_t new_row = k_decode_rows - 1;
    char tbuf[16];
    format_uptime(tbuf, sizeof(tbuf));
    if (strcmp(name, "重力工具面") == 0) {
        lv_table_set_cell_value(g_ui.table_decode, new_row, 0, "GTF");
    } else if (strcmp(name, "磁性工具面") == 0) {
        lv_table_set_cell_value(g_ui.table_decode, new_row, 0, "MTF");
    } else {
        lv_table_set_cell_value(g_ui.table_decode, new_row, 0, name);
    }
    lv_table_set_cell_value(g_ui.table_decode, new_row, 1, value_text);
    lv_table_set_cell_value(g_ui.table_decode, new_row, 2, tbuf);

    for (int col = 0; col < 3; col++) {
        lv_table_clear_cell_ctrl(g_ui.table_decode, new_row, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
    }

    if (highlight) {
        for (int col = 0; col < 3; col++) {
            lv_table_add_cell_ctrl(g_ui.table_decode, new_row, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
        }
    }
}

// ---------------------------------------------------------
// 消息弹窗逻辑
// ---------------------------------------------------------
/*
 * 功能: 关闭消息弹窗
 * 说明: 关闭定时器并隐藏弹窗控件
 */
static void msg_close_cb(lv_event_t *e)
{
    (void)e;
    if (g_ui.msg_timer) {
        lv_timer_del(g_ui.msg_timer);
        g_ui.msg_timer = NULL;
    }
    if (g_ui.msg_cont) {
        lv_obj_add_flag(g_ui.msg_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_ui.msg_label) {
        lv_obj_add_flag(g_ui.msg_label, LV_OBJ_FLAG_HIDDEN);
    }
    g_msg_active = 0;
    g_msg_persistent = 0;

    lv_disp_t *disp = lv_disp_get_default();
    if (disp) {
        lv_refr_now(disp);
    }
}

static void msg_touch_close_cb(lv_event_t *e)
{
    msg_close_cb(e);
}

/*
 * 功能: 弹窗自动关闭定时器回调
 */
static void msg_timer_cb(lv_timer_t *t)
{
    (void)t;
    msg_close_cb(NULL);
}

/*
 * 功能: 显示消息弹窗
 * 参数: auto_close_ms=0 表示常驻显示，>0 表示自动关闭
 */
void dashboard_show_message(const char *text, uint32_t auto_close_ms)
{
    if (!text) {
        return;
    }

    // 固定右下角消息区域：更新文本并显示
    if (g_ui.msg_label && g_ui.msg_cont) {
        build_two_line_msg(text);
        lv_label_set_text_static(g_ui.msg_label, g_msg_text);
        lv_obj_clear_flag(g_ui.msg_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_ui.msg_label, LV_OBJ_FLAG_HIDDEN);
        g_msg_active = 1;
    }

    g_msg_persistent = (auto_close_ms == 0) ? 1 : 0;
    if (auto_close_ms > 0) {
        if (g_ui.msg_timer) {
            lv_timer_del(g_ui.msg_timer);
        }
        g_ui.msg_timer = lv_timer_create(msg_timer_cb, auto_close_ms, NULL);
    } else {
        if (g_ui.msg_timer) {
            lv_timer_del(g_ui.msg_timer);
            g_ui.msg_timer = NULL;
        }
    }
}

/*
 * 功能: 查询弹窗显示状态
 */
int dashboard_message_is_active(void)
{
    return g_msg_active ? 1 : 0;
}

// ---------------------------------------------------------
// 调试小部件刷新
// ---------------------------------------------------------
void dashboard_debug_update(const dashboard_debug_info_t *info)
{
    if (!info || !g_ui.dbg_cont) {
        return;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "DBG: online");
    lv_label_set_text(g_ui.dbg_line1, buf);

    snprintf(buf, sizeof(buf), "RX: %lu  ISR: %lu  TRY: %lu",
             (unsigned long)info->rx_bytes,
             (unsigned long)info->rx_isr,
             (unsigned long)info->try_cnt);
    lv_label_set_text(g_ui.dbg_line2, buf);

    snprintf(buf, sizeof(buf), "OK: %lu  BAD: %lu  BUF: %lu",
             (unsigned long)info->frames_ok,
             (unsigned long)info->frames_bad,
             (unsigned long)info->buf_len);
    lv_label_set_text(g_ui.dbg_line3, buf);

    snprintf(buf, sizeof(buf), "DROP H:%lu L:%lu C:%lu",
             (unsigned long)info->drop_no_header,
             (unsigned long)info->drop_len,
             (unsigned long)info->drop_chk);
    lv_label_set_text(g_ui.dbg_line4, buf);

    if (info->last_err) {
        if (info->last_raw[0] != '\0') {
            snprintf(buf, sizeof(buf), "%s", info->last_raw);
        } else {
            snprintf(buf, sizeof(buf), "CHK: %02X/%02X LEN:%u",
                     info->last_chk, info->last_calc, (unsigned)info->last_len);
        }
        lv_label_set_text(g_ui.dbg_line5, buf);
        return;
    } else if (info->last_name[0] != '\0') {
        char vbuf[16];
        /* 调试面板浮点也走整数拼接，避免小数点显示异常 */
        format_fixed(vbuf, sizeof(vbuf), info->last_value, 2);
        snprintf(buf, sizeof(buf), "LAST: %s=%s", info->last_name, vbuf);
    } else {
        snprintf(buf, sizeof(buf), "TO:%lu ERR:%lu/%lu/%lu/%lu",
                 (unsigned long)info->parse_timeout,
                 (unsigned long)info->err_ore,
                 (unsigned long)info->err_fe,
                 (unsigned long)info->err_ne,
                 (unsigned long)info->err_pe);
    }
    lv_label_set_text(g_ui.dbg_line5, buf);
}

