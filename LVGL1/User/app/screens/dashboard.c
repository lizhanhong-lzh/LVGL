#include "dashboard.h"
#include "../app.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h> // for rand()

/*
 * =========================================================================================
 * 【模块说明 / Module Description】
 * 模块名称: Dashboard UI (仪表盘界面模块)
 * 文件路径: d:\LVGL\LVGL1\User\app\screens\dashboard.c
 * 
 * 功能概述:
 * 本模块负责整个平板主界面的绘制与数据刷新。界面分为左右两个主要区域：
 * 1. 左侧 (55%): 工具面仪表盘 (Toolface Dial)，由5个同心圆弧组成，分别显示不同历史时刻的工具面角度。
 *    - 最外圈 (arcs[4]) 表示最新数据。
 *    - 最内圈 (arcs[0]) 表示最旧数据。
 *    - 颜色根据数据类型 (GTF/MTF) 动态变化 (紫色/蓝色)。
 * 2. 右侧 (45%): 显示核心参数列表和实时解包数据流表格。
 *    - 包含: 井斜(Inc), 方位(Azi), 工具面(TF), 泵压(Pressure), 开泵状态, 电池电压等。
 *    - 数据流表格 (Table Decode) 实时滚动显示接收到的协议包解析结果。
 * 
 * 设计细节:
 * - 字体: 使用自定义的中文字体 (my_font_30) 以支持中文标签显示。
 * - 布局: 使用 Flexbox 布局实现自适应排列。
 * - 兼容性: 针对 Keil MDK 编译器做了 UTF-8 编码适配说明。
 * =========================================================================================
 * 
 * 【重要提示 / IMPORTANT】
 * 本文件包含中文字符。为了确保在 Keil MDK / ARMCC 编译器中显示正常，必须满足以下条件之一：
 * 
 * 1. (推荐) 使用 UTF-8 编码保存此文件 (无 BOM)，并在 Keil 编译选项中添加 "--c99 --no_multibyte_chars" 
 *    或者确保编译器能正确识别 UTF-8 源文件。
 * 2. 如果 Keil 出现乱码，请检查 Edit -> Configuration -> Editor -> Encoding 是否为 Encode in UTF-8 without signature。
 * 
 * 如果板载显示仍然出现“方框”或“缺字”，则说明当前的字体文件 (lv_font_simsun_16_cjk.c) 
 * 缺少对应的汉字字模。请使用 LVGL 字体工具重新生成字体 C 文件，并包含所需的字符。
 * =========================================================================================
 */

// 声明中文字体 (外部资源)
LV_FONT_DECLARE(my_font_30);
LV_FONT_DECLARE(my_font_16);

// ============================================================================
// 数据协议定义 (参考 SQMWD_Tablet/mainwindow.h)
// 这里定义了串口协议中 payload[5] 位置对应的类型码
// ============================================================================
typedef enum {
    /* 核心参数 (Core Parameters) */
    DT_INC   = 0x10, // 井斜 (Inclination) - 也就是井眼的倾斜角度
    DT_AZI   = 0x11, // 方位 (Azimuth) - 井眼相对于北极的方向
    DT_TF    = 0x12, // 工具面 (Toolface - Generic) - 通用工具面角度
    DT_GTF   = 0x13, // 重力工具面 (Gravity TF) - 基于重力计算的工具面 (通常显示为紫色)
    DT_MTF   = 0x14, // 磁性工具面 (Magnetic TF) - 基于磁场计算的工具面 (通常显示为蓝色)
    DT_DIP   = 0x15, // 磁倾角 (Dip Angle)
    DT_TEMP  = 0x16, // 温度 (Temperature) - 井下探管温度
    DT_VOLT  = 0x17, // 电池电压 (Battery) - 探管电池电量监测
    
    /* 扩展参数 (如有) */
    DT_GRAV_TOTAL = 0x1F, // 总重力 (G-Total)
    DT_MAG_TOTAL  = 0x20, // 总磁场 (H-Total)
} probe_data_type_t;

// ---------------------------------------------------------
// UI 结构体定义 (UI Context)
// 保存界面中需要动态更新的 LVGL 对象指针
// ---------------------------------------------------------
typedef struct {
    // 左侧：仪表盘 (5个同心圆弧)
    // arcs[0]是内圈(历史), arcs[4]是外圈(最新)
    lv_obj_t *arcs[5]; 

    // 右侧：数值显示 label 对象 (列表形式)
    lv_obj_t *label_inc;      // 井斜数值 Label
    lv_obj_t *label_azi;      // 方位数值 Label
    lv_obj_t *label_tf;       // 工具面数值 Label
    lv_obj_t *label_pump;     // 泵压数值 Label
    lv_obj_t *label_pump_status; // 开泵状态文本 Label ("开泵"/"关泵")

    // 列表/日志容器
    lv_obj_t *table_cont;     // 表格的外层滚动容器 (Container)
    lv_obj_t *table_decode;   // 解码数据表 (Table Object)
    
    // 顶部状态栏
    lv_obj_t *label_comm_info; // 通讯信息 Label (如 "COM1 通信中")

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
static const uint32_t k_decode_rows = 15;
static uint32_t g_decode_row = 0;

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

// ---------------------------------------------------------
// 辅助函数：创建数据行 (Label + Value)
// 目的：把“标题 + 数值”组成一行，供右侧数据列表复用
// 参数:
//   parent: 父对象 (容器)
//   title:  左侧显示的标题文本 (如"井斜")
//   val_label_out: 输出参数，返回右侧数值 Label 指针用于动态刷新
// ---------------------------------------------------------
static lv_obj_t *create_data_row(lv_obj_t *parent, const char *title, lv_obj_t **val_label_out)
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

    // 使用 Flex 布局：左右两端对齐 (Space Between)
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // [左侧] 标题 Label
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &my_font_30, 0); // 使用中文字体
    lv_obj_set_style_text_color(label, lv_color_hex(0x414243), 0); // 深灰色文本
    lv_obj_set_style_min_width(label, 60, 0); // 最小宽度保证对齐
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP); // 禁止文字竖向换行

    // [右侧] 数值 Label：只显示数字，避免中文字体缺字造成异常
    lv_obj_t *val = lv_label_create(cont);
    lv_label_set_text(val, "0.00"); // 默认初始值
    lv_obj_set_style_text_font(val, &lv_font_montserrat_28, 0); // 使用大号蒙纳英文字体
    lv_obj_set_style_text_color(val, lv_color_hex(0x002FA7), 0); // 克莱因蓝 (高亮显示数值)
    // 让数值稍微靠左一点避免贴边
    lv_obj_set_style_pad_right(val, 5, 0);

    // 将数值 Label 的指针赋值给输出参数
    *val_label_out = val;
    return cont;
}

// ---------------------------------------------------------
// 仪表盘创建：5个同心圆弧 (Toolface Dial)
// 功能: 在屏幕左侧创建一个包含5层圆弧的仪表盘，用于可视化工具面历史趋势
// 说明: 外圈代表最新数据，内圈代表历史数据
// ---------------------------------------------------------
static lv_obj_t *create_toolface_dial(lv_obj_t *parent)
{
    // 创建仪表盘容器
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 600, 600); // 放大仪表盘容器
    lv_obj_set_style_bg_opa(cont, 0, 0); // 透明背景
    lv_obj_set_style_border_width(cont, 0, 0); // 无边框
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 0); // 居中显示

    int max_r = 270; /// 最外圈半径
    int ring_w = 28; /// 圆环宽度
    int ring_gap = 6;/// 圆环间距
    
    // 循环创建5个圆环 (i=0为最内圈, i=4为最外圈)
    for (int i = 0; i < 5; i++) {
        // 计算当前圆环的半径: 外圈大，内圈小
        int current_r = max_r - (4 - i) * (ring_w + ring_gap);
        int size = current_r * 2;

        // 创建 Arc 对象
        lv_obj_t *arc = lv_arc_create(cont);
        lv_obj_set_size(arc, size, size);
        lv_arc_set_rotation(arc, 270); // 旋转起点到 12点钟方向 (LVGL默认0度在3点钟)
        lv_arc_set_bg_angles(arc, 0, 360); // 背景圆弧占满一圈
        lv_arc_set_range(arc, 0, 360); // 设置数值范围为 0-360 度
        lv_arc_set_value(arc, 0); // 初始值 0
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL); 

        lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0); // 也是居中叠加
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB); // 移除旋钮 (Knob)，变成纯显示仪表
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE); // 禁止用户点击交互

        // 样式 - 背景 (未填充部分)
        lv_obj_set_style_arc_width(arc, ring_w, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0xE0E0E0), LV_PART_MAIN); // 浅灰色底
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN); // 平头端点

        // 样式 - 指示器 (填充部分)
        lv_obj_set_style_arc_width(arc, ring_w, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0x002FA7), LV_PART_INDICATOR); // 默认蓝色
        
        // 透明度渐变效果: 内圈更淡 (历史久) -> 外圈更深 (最新)
        // 透明度从内到外逐层递增
        int opacities[] = {80, 120, 160, 210, 255};
        lv_obj_set_style_arc_opa(arc, (lv_opa_t)opacities[i], LV_PART_INDICATOR);
        
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR); 

        // 保存对象指针以便后续 update 使用
        g_ui.arcs[i] = arc;
    }

    // 绘制四周角度刻度标签 (0, 30, 60 ...)
    int label_r = max_r + 10; // 标签半径内收，避免遮挡
    for (int deg = 0; deg < 360; deg += 30) {
        lv_obj_t *lbl = lv_label_create(cont);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", deg);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0); // 放大刻度数字
        lv_obj_set_style_text_color(lbl, lv_color_black(), 0);

        // 计算标签位置 (极坐标转直角坐标)
        double rad = (double)deg * 3.1415926535 / 180.0;
        int x_offset = (int)(label_r * sin(rad));
        int y_offset = (int)(-label_r * cos(rad)); // Y轴向下为正，cos(0)在上方需要负号
        lv_obj_align(lbl, LV_ALIGN_CENTER, x_offset, y_offset);
    }
    return cont;
}

// ---------------------------------------------------------
// UI 主构建函数
// 功能: 初始化整个仪表盘界面，创建所有LVGL对象
// ---------------------------------------------------------
lv_obj_t *dashboard_create(void)
{
    // 1. 创建根页面 (Root Screen)
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0); // 设置背景为白色
    lv_obj_set_style_pad_all(scr, 5, 0); // 全局 5px 内边距
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW); // 设置为水平布局 (左右分栏)
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE); // 根页面禁止滚动

    // 2. 左侧：仪表盘区域 (Left Panel)
    lv_obj_t *left_panel = lv_obj_create(scr);
    lv_obj_set_size(left_panel, LV_PCT(55), LV_PCT(100)); // 宽度占 55%，高度占满
    lv_obj_set_style_border_width(left_panel, 0, 0);      // 无边框
    lv_obj_clear_flag(left_panel, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动
    lv_obj_set_flex_flow(left_panel, LV_FLEX_FLOW_COLUMN); // 内部垂直居中
    lv_obj_set_flex_align(left_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 调用辅助函数创建同心圆仪表盘
    create_toolface_dial(left_panel);

    // 3. 右侧：数据与列表区域 (Right Panel)
    lv_obj_t *right_panel = lv_obj_create(scr);
    lv_obj_set_size(right_panel, LV_PCT(45), LV_PCT(100)); // 宽度占 45%
    lv_obj_set_style_border_width(right_panel, 0, 0); // 无边框
    lv_obj_set_style_pad_all(right_panel, 4, 0); // 少量内边距
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN); // 内部垂直布局 (上:列表, 下:表格)
    
    // 3.1 数据列表区 (Top Data List) - 显示实时值
    lv_obj_t *data_list_cont = lv_obj_create(right_panel);
    lv_obj_set_width(data_list_cont, LV_PCT(100));
    lv_obj_set_height(data_list_cont, LV_SIZE_CONTENT); // 高度自适应内容
    lv_obj_set_style_border_width(data_list_cont, 1, 0); // 外框细线
    lv_obj_set_style_border_color(data_list_cont, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_pad_all(data_list_cont, 5, 0);
    lv_obj_clear_flag(data_list_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(data_list_cont, LV_FLEX_FLOW_COLUMN); // 垂直排列

    // 3.1.1 标题行/通讯状态栏
    lv_obj_t *header_row = lv_obj_create(data_list_cont);
    lv_obj_set_size(header_row, LV_PCT(100), 30); // 固定高度
    lv_obj_set_style_border_width(header_row, 0, 0);
    lv_obj_set_style_bg_opa(header_row, 0, 0);
    lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW); // 水平两端对齐
    lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // 左上角标题
    lv_obj_t *lbl_title = lv_label_create(header_row);
    lv_label_set_text(lbl_title, "SQMWD");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_12, 0);
    
    // 右上角通讯状态 Label
    g_ui.label_comm_info = lv_label_create(header_row);
    lv_label_set_text(g_ui.label_comm_info, "COM.. --"); // 初始
    lv_obj_set_style_text_font(g_ui.label_comm_info, &my_font_30, 0); 
    lv_obj_set_style_text_color(g_ui.label_comm_info, lv_color_hex(0x666666), 0);
    
    // 3.1.2 创建各项数据行
    // 使用 create_data_row 辅助函数批量创建
    create_data_row(data_list_cont, "井斜 Inc", &g_ui.label_inc); 
    create_data_row(data_list_cont, "方位 Azi", &g_ui.label_azi); 
    create_data_row(data_list_cont, "工具面 TF", &g_ui.label_tf); 
    create_data_row(data_list_cont, "泵压 MPa", &g_ui.label_pump); 
    create_data_row(data_list_cont, "状态 Status", &g_ui.label_pump_status);
    
    // 强制 "状态" 值使用支持中文的字体 (因为要显示 "开泵"/"关泵")
    lv_obj_set_style_text_font(g_ui.label_pump_status, &my_font_30, 0);

    // 3.2 解码数据表格 (Bottom Table) - 滚动显示历史记录
    // 说明：表头与数据表分离，保证表头固定，数据可滚动
    // 3.2.1 固定的表头 (使用 1 行 Table 实现对齐，不随数据滚动)
    lv_obj_t *table_header = lv_table_create(right_panel);
    lv_obj_set_width(table_header, LV_PCT(100));
    lv_obj_set_height(table_header, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(table_header, 0, 0);
    lv_obj_set_style_border_width(table_header, 0, 0);
    lv_obj_set_style_radius(table_header, 0, 0);
    lv_obj_set_style_bg_opa(table_header, LV_OPA_TRANSP, 0);
    
    // 设置表头中文字体
    lv_obj_set_style_text_font(table_header, &my_font_30, LV_PART_ITEMS);
    // 配置列宽 (共3列)
    lv_table_set_col_cnt(table_header, 2);
    lv_table_set_col_width(table_header, 0, 190); // 参数
    lv_table_set_col_width(table_header, 1, 190); // 解码值
    lv_obj_set_style_pad_all(table_header, 4, LV_PART_ITEMS);
    // 填充表头文本
    lv_table_set_cell_value(table_header, 0, 0, "参数");
    lv_table_set_cell_value(table_header, 0, 1, "解码值");

    // 3.2.2 数据列表容器 (可滚动)
    g_ui.table_cont = lv_obj_create(right_panel);
    lv_obj_set_width(g_ui.table_cont, LV_PCT(100));
    lv_obj_set_flex_grow(g_ui.table_cont, 1); // 占据剩余所有垂直空间
    lv_obj_set_style_pad_all(g_ui.table_cont, 0, 0);
    lv_obj_set_style_border_width(g_ui.table_cont, 1, 0);
    lv_obj_set_style_border_color(g_ui.table_cont, lv_color_hex(0xCCCCCC), 0);
    lv_obj_add_flag(g_ui.table_cont, LV_OBJ_FLAG_SCROLLABLE); // 允许滚动

    // 3.2.3 实际数据表格
    // 说明：使用中文字体展示参数名称；数值显示走 format_fixed，避免小数点异常
    g_ui.table_decode = lv_table_create(g_ui.table_cont);
    lv_obj_set_width(g_ui.table_decode, LV_PCT(100)); 
    
    // 设置表格内容字体
    lv_obj_set_style_text_font(g_ui.table_decode, &my_font_16, LV_PART_ITEMS);

    // 为同步头行准备高亮样式 (使用 USER_1 状态)
    lv_obj_set_style_bg_opa(g_ui.table_decode, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_USER_1);
    lv_obj_set_style_bg_color(g_ui.table_decode, lv_color_hex(0xA5D6A7), LV_PART_ITEMS | LV_STATE_USER_1);
    lv_obj_set_style_text_color(g_ui.table_decode, lv_color_black(), LV_PART_ITEMS | LV_STATE_USER_1);

    // 配置列宽 (必须与表头一致)
    lv_table_set_col_cnt(g_ui.table_decode, 2);
    lv_table_set_col_width(g_ui.table_decode, 0, 190); 
    lv_table_set_col_width(g_ui.table_decode, 1, 190); 
    lv_obj_set_style_pad_all(g_ui.table_decode, 4, LV_PART_ITEMS);  

    lv_table_set_row_cnt(g_ui.table_decode, k_decode_rows);
    for (uint32_t r = 0; r < k_decode_rows; r++) {
        lv_table_set_cell_value(g_ui.table_decode, r, 0, "");
        lv_table_set_cell_value(g_ui.table_decode, r, 1, "");
    }

    // 4. 固定消息区域（右下角，置于顶层不遮挡主布局）
    g_ui.msg_cont = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_ui.msg_cont, 320, 120);
    lv_obj_set_style_bg_color(g_ui.msg_cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_ui.msg_cont, LV_OPA_70, 0);
    lv_obj_set_style_border_width(g_ui.msg_cont, 1, 0);
    lv_obj_set_style_border_color(g_ui.msg_cont, lv_color_hex(0x666666), 0);
    lv_obj_set_style_pad_all(g_ui.msg_cont, 6, 0);
    lv_obj_set_style_radius(g_ui.msg_cont, 6, 0);
    lv_obj_align(g_ui.msg_cont, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_add_flag(g_ui.msg_cont, LV_OBJ_FLAG_HIDDEN);

    g_ui.msg_label = lv_label_create(g_ui.msg_cont);
    lv_label_set_long_mode(g_ui.msg_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_ui.msg_label, LV_PCT(100));
    lv_label_set_text(g_ui.msg_label, "");
    lv_obj_set_style_text_color(g_ui.msg_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_ui.msg_label, &my_font_16, 0);
    lv_obj_set_style_text_align(g_ui.msg_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(g_ui.msg_label);

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
    
    return scr;
}

// ---------------------------------------------------------
// 界面更新函数
// 功能: 被上层 (如串口解析逻辑) 调用，将最新业务数据刷新到界面
// 参数: 
//   data: 最新的设备状态数据结构体指针
// ---------------------------------------------------------
void dashboard_update(const plant_metrics_t *data)
{
    char buf[32];

    // 1. 更新顶部实时数据列表 (Real-time List)
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
    
    // 更新泵压
    format_fixed(buf, sizeof(buf), data->pump_pressure, 1);
    lv_label_set_text(g_ui.label_pump, buf);

    // 更新开泵状态（同时调整颜色提示）
    if (data->pump_status) {
        lv_label_set_text(g_ui.label_pump_status, "开      泵"); 
        lv_obj_set_style_text_color(g_ui.label_pump_status, lv_color_hex(0x00AA00), 0); // 绿色 (Green)
    } else {
        lv_label_set_text(g_ui.label_pump_status, "关      泵");
        lv_obj_set_style_text_color(g_ui.label_pump_status, lv_color_hex(0xFF0000), 0); // 红色 (Red)
    }

    // 更新通讯状态（连接/断开状态提示）
    if (data->port_connected) {
        // e.g. "COM1 通信中"
        snprintf(buf, sizeof(buf), "%s 通信中", data->port_name); 
        lv_obj_set_style_text_color(g_ui.label_comm_info, lv_color_hex(0x228B22), 0); // 森林绿
    } else {
        // e.g. "COM 无信号"
        snprintf(buf, sizeof(buf), "%s 无信号", data->port_name[0] ? data->port_name : "COM"); 
        lv_obj_set_style_text_color(g_ui.label_comm_info, lv_color_hex(0xB22222), 0); // 火砖红
    }
    lv_label_set_text(g_ui.label_comm_info, buf);


    // 2. 更新仪表盘 (Toolface Dial)
    // ------------------------------------------------
    // 遍历5个圆环，将历史队列中的数据映射上去
    for (int i = 0; i < 5; i++) {
        // arcs[i] 对应: 0=内圈(旧数据), 4=外圈(新数据)
        // history[i] 对应: 0=最旧时刻, 4=最新时刻
        // data->toolface_history 是一个 FIFO 队列，由上层维护移位
        float val = data->toolface_history[i];

        // 工业显示要求：工具面固定为蓝色，不随类型切换
        lv_obj_set_style_arc_color(g_ui.arcs[i], lv_color_hex(0x002FA7), LV_PART_INDICATOR);

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
void dashboard_append_decode_row(const char *name, float value, int highlight)
{
    if (!g_ui.table_decode || !name) {
        return;
    }

    uint32_t new_row = g_decode_row % k_decode_rows;
    g_decode_row++;

    char val_str[24];
    format_fixed(val_str, sizeof(val_str), value, 2);

    lv_table_set_cell_value(g_ui.table_decode, new_row, 0, name);
    lv_table_set_cell_value(g_ui.table_decode, new_row, 1, val_str);

    /* 清除上一轮可能遗留的高亮标记 */
    for (int col = 0; col < 2; col++) {
        lv_table_clear_cell_ctrl(g_ui.table_decode, new_row, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
    }

    /* 需要高亮时再加标记（同步头等场景） */
    if (highlight) {
        for (int col = 0; col < 2; col++) {
            lv_table_add_cell_ctrl(g_ui.table_decode, new_row, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
        }
    }
}

// ---------------------------------------------------------
// 追加一条解码数据行（字符串值）
// 用途：兼容 Tablet 中“序列/QID”等非数值显示
// ---------------------------------------------------------
void dashboard_append_decode_text_row(const char *name, const char *value_text, int highlight)
{
    if (!g_ui.table_decode || !name || !value_text) {
        return;
    }

    uint32_t new_row = g_decode_row % k_decode_rows;
    g_decode_row++;

    lv_table_set_cell_value(g_ui.table_decode, new_row, 0, name);
    lv_table_set_cell_value(g_ui.table_decode, new_row, 1, value_text);

    for (int col = 0; col < 2; col++) {
        lv_table_clear_cell_ctrl(g_ui.table_decode, new_row, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
    }

    if (highlight) {
        for (int col = 0; col < 2; col++) {
            lv_table_add_cell_ctrl(g_ui.table_decode, new_row, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
        }
    }
}

// ---------------------------------------------------------
// 消息弹窗逻辑
// ---------------------------------------------------------
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
}

static void msg_timer_cb(lv_timer_t *t)
{
    (void)t;
    msg_close_cb(NULL);
}

void dashboard_show_message(const char *text, uint32_t auto_close_ms)
{
    if (!text) {
        return;
    }

    // 固定右下角消息区域：更新文本并显示
    if (g_ui.msg_label && g_ui.msg_cont) {
        lv_label_set_text(g_ui.msg_label, text);
        lv_obj_clear_flag(g_ui.msg_cont, LV_OBJ_FLAG_HIDDEN);
    }

    if (auto_close_ms > 0) {
        if (g_ui.msg_timer) {
            lv_timer_del(g_ui.msg_timer);
        }
        g_ui.msg_timer = lv_timer_create(msg_timer_cb, auto_close_ms, NULL);
    }
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

