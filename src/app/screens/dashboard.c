#include "dashboard.h"
#include "../app.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h> // for rand()

/*
 * =========================================================================================
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
 * 
 * dashboard.c
 * 功能：SQMWD 平板主界面
 */

// 声明中文字体
LV_FONT_DECLARE(my_font_30);

// ============================================================================
// 数据协议定义 (参考 SQMWD_Tablet/mainwindow.h)
// ============================================================================
typedef enum {
    DT_INC   = 0x10, // 16: 井斜 (Inclination)
    DT_AZI   = 0x11, // 17: 方位 (Azimuth)
    DT_TF    = 0x12, // 18: 工具面 (Toolface)
    DT_TEMP  = 0x14, // 20: 温度 (Temperature)
    DT_BATT  = 0x19, // 25: 电池电压 (Battery)
    DT_DIP   = 0x16, // 22: 磁倾角 (Dip Angle)
    DT_GRAV  = 0x18, // 24: 重力场 (Gravity)
    DT_MAG   = 0x17, // 23: 磁场 (Magnetic Field)
} probe_data_type_t;

// ---------------------------------------------------------
// UI 结构体定义
// ---------------------------------------------------------
typedef struct {
    // 左侧：仪表盘 (5个同心圆弧)
    lv_obj_t *arcs[5]; 

    // 右侧：数值显示 (列表形式)
    lv_obj_t *label_inc;      // 井斜
    lv_obj_t *label_azi;      // 方位
    lv_obj_t *label_tf;       // 工具面
    lv_obj_t *label_pump;     // 泵压
    lv_obj_t *label_pump_status; // 开泵状态 (作为数值行显示)

    // 列表/日志容器
    lv_obj_t *table_cont;     // 表格的滚动容器
    lv_obj_t *table_decode;   // 解码数据表
    
    // 顶部状态栏
    lv_obj_t *label_comm_info; // 通讯信息 (COM1 已连接)

    // 消息区域（固定左下角）
    lv_obj_t *msg_cont;
    lv_obj_t *msg_label;
    lv_timer_t *msg_timer;

} dashboard_ui_t;

static dashboard_ui_t g_ui;

// ---------------------------------------------------------
// 辅助函数：创建数据行 (Label + Value)
// ---------------------------------------------------------
static lv_obj_t *create_data_row(lv_obj_t *parent, const char *title, lv_obj_t **val_label_out)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_width(cont, LV_PCT(100));
    lv_obj_set_height(cont, LV_SIZE_CONTENT); // 高度自适应
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0); // 无边框
    // 只在底部加一条分割线
    lv_obj_set_style_border_width(cont, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(cont, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(cont, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(cont, 6, 0); // 上下间距
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // 使用 Flex 布局：左右撑开
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 标题 (左侧)
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &my_font_30, 0); 
    lv_obj_set_style_text_color(label, lv_color_hex(0x414243), 0);
    lv_obj_set_style_min_width(label, 60, 0); // 最小宽度
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP); // 禁止文字竖向换行

    // 数值 (右侧)
    lv_obj_t *val = lv_label_create(cont);
    lv_label_set_text(val, "0.00");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_28, 0); // 大字体数值
    lv_obj_set_style_text_color(val, lv_color_hex(0x002FA7), 0); // 克莱因蓝
    // 让数值稍微靠左一点避免贴边
    lv_obj_set_style_pad_right(val, 5, 0);

    *val_label_out = val;
    return cont;
}

// ---------------------------------------------------------
// 仪表盘创建：5个同心圆弧
// ---------------------------------------------------------
static lv_obj_t *create_toolface_dial(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 600, 600); // 放大容器
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 0);

    int max_r = 270; 
    int ring_w = 28;
    int ring_gap = 6;
    
    for (int i = 0; i < 5; i++) {
        int current_r = max_r - (4 - i) * (ring_w + ring_gap);
        int size = current_r * 2;

        lv_obj_t *arc = lv_arc_create(cont);
        lv_obj_set_size(arc, size, size);
        lv_arc_set_rotation(arc, 270); 
        lv_arc_set_bg_angles(arc, 0, 360); 
        lv_arc_set_range(arc, 0, 360); // 设置范围为0-360度，对应工具面角度
        lv_arc_set_value(arc, 0); 
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL); 

        lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB); 
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

        // 样式 - 背景
        lv_obj_set_style_arc_width(arc, ring_w, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0xE0E0E0), LV_PART_MAIN); 
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN); 

        // 样式 - 指示器
        lv_obj_set_style_arc_width(arc, ring_w, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0x002FA7), LV_PART_INDICATOR);
        
        // 透明度: 内圈更淡(旧) -> 外圈更深(新)
        // arcs[i] 半径由小到大 (i=0 内, i=4 外)
        int opacities[] = {80, 120, 160, 210, 255};
        lv_obj_set_style_arc_opa(arc, (lv_opa_t)opacities[i], LV_PART_INDICATOR);
        
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR); 

        g_ui.arcs[i] = arc;
    }

    // 绘制四周角度标签
    int label_r = max_r + 10;
    for (int deg = 0; deg < 360; deg += 30) {
        lv_obj_t *lbl = lv_label_create(cont);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", deg);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0); // 放大刻度数字
        lv_obj_set_style_text_color(lbl, lv_color_black(), 0);

        double rad = (double)deg * 3.1415926535 / 180.0;
        int x_offset = (int)(label_r * sin(rad));
        int y_offset = (int)(-label_r * cos(rad));
        lv_obj_align(lbl, LV_ALIGN_CENTER, x_offset, y_offset);
    }
    return cont;
}

// ---------------------------------------------------------
// UI 创建
// ---------------------------------------------------------
lv_obj_t *dashboard_create(void)
{
    // 根页面
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_pad_all(scr, 5, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // 左侧：仪表盘
    lv_obj_t *left_panel = lv_obj_create(scr);
    lv_obj_set_size(left_panel, LV_PCT(55), LV_PCT(100)); // 调整为 55%
    lv_obj_set_style_border_width(left_panel, 0, 0);
    lv_obj_clear_flag(left_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(left_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_toolface_dial(left_panel);

    // 右侧：数据与列表
    lv_obj_t *right_panel = lv_obj_create(scr);
    lv_obj_set_size(right_panel, LV_PCT(45), LV_PCT(100)); // 调整为 45%，增加宽度以防文字挤压
    lv_obj_set_style_border_width(right_panel, 0, 0);
    lv_obj_set_style_pad_all(right_panel, 4, 0);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN);
    
    // 1. 数据列表区
    lv_obj_t *data_list_cont = lv_obj_create(right_panel);
    lv_obj_set_width(data_list_cont, LV_PCT(100));
    lv_obj_set_height(data_list_cont, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(data_list_cont, 1, 0);
    lv_obj_set_style_border_color(data_list_cont, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_pad_all(data_list_cont, 5, 0);
    lv_obj_clear_flag(data_list_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(data_list_cont, LV_FLEX_FLOW_COLUMN); 

    // 标题行/通讯状态
    lv_obj_t *header_row = lv_obj_create(data_list_cont);
    lv_obj_set_size(header_row, LV_PCT(100), 30);
    lv_obj_set_style_border_width(header_row, 0, 0);
    lv_obj_set_style_bg_opa(header_row, 0, 0);
    lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t *lbl_title = lv_label_create(header_row);
    lv_label_set_text(lbl_title, "SQMWD");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_12, 0);
    
    // 通讯状态 Label
    g_ui.label_comm_info = lv_label_create(header_row);
    lv_label_set_text(g_ui.label_comm_info, "COM.. --"); // 初始
    lv_obj_set_style_text_font(g_ui.label_comm_info, &my_font_30, 0); 
    lv_obj_set_style_text_color(g_ui.label_comm_info, lv_color_hex(0x666666), 0);
    
    // 数据行 (Keil MDK/ARMCC 5 兼容性: 采用新字库后可直接显示)
    create_data_row(data_list_cont, "井斜 Inc", &g_ui.label_inc); 
    create_data_row(data_list_cont, "方位 Azi", &g_ui.label_azi); 
    create_data_row(data_list_cont, "工具面 TF", &g_ui.label_tf); 
    create_data_row(data_list_cont, "泵压 MPa", &g_ui.label_pump); // "泵  压" -> "泵压 MPa"
    create_data_row(data_list_cont, "状态 Status", &g_ui.label_pump_status);
    // 强制状态值使用支持中文的字体
    lv_obj_set_style_text_font(g_ui.label_pump_status, &my_font_30, 0);

    // 2. 解码列表区
    // 固定的表头 (使用 1 行 Table 实现对齐)
    lv_obj_t *table_header = lv_table_create(right_panel);
    lv_obj_set_width(table_header, LV_PCT(100));
    lv_obj_set_height(table_header, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(table_header, 0, 0);
    lv_obj_set_style_border_width(table_header, 0, 0);
    lv_obj_set_style_radius(table_header, 0, 0);
    lv_obj_set_style_bg_opa(table_header, LV_OPA_TRANSP, 0);
    
    lv_obj_set_style_text_font(table_header, &my_font_30, LV_PART_ITEMS);
    lv_table_set_col_cnt(table_header, 2);
    lv_table_set_col_width(table_header, 0, 180); 
    lv_table_set_col_width(table_header, 1, 180); 
    lv_obj_set_style_pad_all(table_header, 6, LV_PART_ITEMS);
    lv_table_set_cell_value(table_header, 0, 0, "参数");
    lv_table_set_cell_value(table_header, 0, 1, "解码值");

    // 数据列表容器 (可滚动)
    g_ui.table_cont = lv_obj_create(right_panel);
    lv_obj_set_width(g_ui.table_cont, LV_PCT(100));
    lv_obj_set_flex_grow(g_ui.table_cont, 1); 
    lv_obj_set_style_pad_all(g_ui.table_cont, 0, 0);
    lv_obj_set_style_border_width(g_ui.table_cont, 1, 0);
    lv_obj_set_style_border_color(g_ui.table_cont, lv_color_hex(0xCCCCCC), 0);
    lv_obj_add_flag(g_ui.table_cont, LV_OBJ_FLAG_SCROLLABLE); 

    g_ui.table_decode = lv_table_create(g_ui.table_cont);
    lv_obj_set_width(g_ui.table_decode, LV_PCT(100)); 
    
    // 设置表格字体
    lv_obj_set_style_text_font(g_ui.table_decode, &my_font_30, LV_PART_ITEMS);

    lv_table_set_col_cnt(g_ui.table_decode, 2);
    lv_table_set_col_width(g_ui.table_decode, 0, 180); 
    lv_table_set_col_width(g_ui.table_decode, 1, 180); 
    lv_obj_set_style_pad_all(g_ui.table_decode, 6, LV_PART_ITEMS);  

    // 3. 固定消息区域（右下角，置于顶层不遮挡主布局）
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
    lv_obj_set_style_text_font(g_ui.msg_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(g_ui.msg_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(g_ui.msg_label);
    
    // 不再需要设置 Row 0 为表头，Row 0 开始直接存数据

    return scr;
}

// ---------------------------------------------------------
// 更新函数
// ---------------------------------------------------------
void dashboard_update(const plant_metrics_t *data)
{
    char buf[32];

    // 1. 更新顶部实时数据列表
    snprintf(buf, sizeof(buf), "%.2f", data->inclination);
    lv_label_set_text(g_ui.label_inc, buf);
    
    snprintf(buf, sizeof(buf), "%.2f", data->azimuth);
    lv_label_set_text(g_ui.label_azi, buf);
    
    snprintf(buf, sizeof(buf), "%.1f", data->toolface);
    lv_label_set_text(g_ui.label_tf, buf);
    
    snprintf(buf, sizeof(buf), "%.1f", data->pump_pressure);
    lv_label_set_text(g_ui.label_pump, buf);

    // 更新开泵状态
    if (data->pump_status) {
        lv_label_set_text(g_ui.label_pump_status, "开      泵"); 
        lv_obj_set_style_text_color(g_ui.label_pump_status, lv_color_hex(0x00AA00), 0); // Green
    } else {
        lv_label_set_text(g_ui.label_pump_status, "关      泵");
        lv_obj_set_style_text_color(g_ui.label_pump_status, lv_color_hex(0xFF0000), 0); // Red
    }

    // 更新通讯状态
    if (data->port_connected) {
        // e.g. "COM1 已连接"
        snprintf(buf, sizeof(buf), "%s 通信中", data->port_name); // "已连接" -> "通信中"
        lv_obj_set_style_text_color(g_ui.label_comm_info, lv_color_hex(0x228B22), 0); 
    } else {
        snprintf(buf, sizeof(buf), "%s 无信号", data->port_name[0] ? data->port_name : "COM"); // "断开" -> "无信号"
        lv_obj_set_style_text_color(g_ui.label_comm_info, lv_color_hex(0xB22222), 0);
    }
    lv_label_set_text(g_ui.label_comm_info, buf);


    // 2. 更新仪表盘 (5个同心圆)
    for (int i = 0; i < 5; i++) {
        // arcs[i] 对应: 0=内圈(旧), 4=外圈(新)
        // history[i] 对应: 0=旧, 4=新
        float val = data->toolface_history[i];

        // 工业显示要求：工具面固定为蓝色，不随类型切换
        lv_obj_set_style_arc_color(g_ui.arcs[i], lv_color_hex(0x002FA7), LV_PART_INDICATOR);

        // 角度严格一一对应：0~360，超范围截断
        int32_t angle = (int32_t)lrintf(val);
        if (angle < 0) {
            angle = 0;
        } else if (angle > 360) {
            angle = 360;
        }
        lv_arc_set_angles(g_ui.arcs[i], 0, angle);
    }

    // 3. 解码表由上层协议解析调用 dashboard_append_decode_row() 刷新
}

// ---------------------------------------------------------
// 追加一条解码数据行（与板端一致）
// ---------------------------------------------------------
void dashboard_append_decode_row(const char *name, float value, int highlight)
{
    if (!g_ui.table_decode || !name) {
        return;
    }

    uint32_t row_count = lv_table_get_row_cnt(g_ui.table_decode);
    const uint32_t max_rows = 60;
    if (row_count >= max_rows) {
        for (uint32_t r = 1; r < row_count; r++) {
            for (uint32_t c = 0; c < 2; c++) {
                const char *v = lv_table_get_cell_value(g_ui.table_decode, r, c);
                lv_table_set_cell_value(g_ui.table_decode, r - 1, c, v ? v : "");
            }
        }
        row_count = max_rows - 1;
    }
    uint32_t new_row = row_count;

    char val_str[24];
    snprintf(val_str, sizeof(val_str), "%.2f", value);

    lv_table_set_cell_value(g_ui.table_decode, new_row, 0, name);
    lv_table_set_cell_value(g_ui.table_decode, new_row, 1, val_str);

    if (highlight) {
        for (int col = 0; col < 2; col++) {
            lv_table_add_cell_ctrl(g_ui.table_decode, new_row, col, LV_TABLE_CELL_CTRL_CUSTOM_1);
        }
    }

    lv_obj_scroll_to_y(g_ui.table_cont, LV_COORD_MAX, LV_ANIM_OFF);
}

// ---------------------------------------------------------
// 消息弹窗逻辑（模拟器）
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

    // 固定左下角消息区域：更新文本并显示
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
