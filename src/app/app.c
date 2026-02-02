#include "app.h"

#include "screens/dashboard.h"
#include "obuf.h"

#include <string.h>
#include <math.h> // for sin
#include <ctype.h>

static plant_metrics_t g_sim;

static uint8_t g_rx_storage[4096];
static obuf_t g_rx_buf;

static void sim_timer_cb(lv_timer_t *t);

typedef struct {
    uint8_t cmd;
    uint8_t sub_cmd;
    uint8_t fid;
    float f1;
    float f2;
    float auto_close_sec;
    char text[128];
    uint8_t has_fid;
    uint8_t has_f2;
    uint8_t has_text;
} sx_frame_t;

static uint8_t xor_checksum(const uint8_t *data, size_t n)
{
    uint8_t x = 0;
    for (size_t i = 0; i < n; i++) {
        x ^= data[i];
    }
    return x;
}

static size_t sx_build_frame_09(const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_cap)
{
    const size_t header_len = 4;
    const size_t total = header_len + payload_len + 1;
    if (out_cap < total) {
        return 0;
    }

    out[0] = 0x40;
    out[1] = 0x46;
    out[2] = 0x09;
    out[3] = (uint8_t)payload_len;
    if (payload_len > 0) {
        memcpy(out + 4, payload, payload_len);
    }
    out[4 + payload_len] = xor_checksum(out, 4 + payload_len);
    return total;
}

static size_t sx_build_frame_pump(float f1, float f2, uint8_t *out, size_t out_cap)
{
    uint8_t payload[1 + 4 + 4];
    payload[0] = 0x01; /* Sub_CMD=0x01 */
    memcpy(&payload[1], &f1, sizeof(float));
    memcpy(&payload[5], &f2, sizeof(float));
    return sx_build_frame_09(payload, sizeof(payload), out, out_cap);
}

static size_t sx_build_frame_name(float f1, const char *name, uint8_t *out, size_t out_cap)
{
    if (!name) return 0;
    size_t name_len = strlen(name);
    if (name_len > 64) name_len = 64; /* 控制帧长度 */

    uint8_t payload[1 + 1 + 4 + 64];
    payload[0] = 0x02; /* Sub_CMD=0x02 */
    payload[1] = 0x01; /* FID 占位 */
    memcpy(&payload[2], &f1, sizeof(float));
    memcpy(&payload[6], name, name_len);
    return sx_build_frame_09(payload, 6 + name_len, out, out_cap);
}

static void obuf_write_frag(obuf_t *o, const uint8_t *data, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        size_t chunk = 8 + (pos % 11); /* 8~18 字节分片，模拟串口半包 */
        if (pos + chunk > len) chunk = len - pos;
        obuf_write(o, data + pos, chunk);
        pos += chunk;
    }
}

static int sx_try_parse_one(obuf_t *in, sx_frame_t *out)
{
    const uint8_t header[2] = {0x40, 0x46};

    for (int guard = 0; guard < 64; guard++) {
        int off = obuf_find(in, header, sizeof(header));
        if (off < 0) {
            size_t len = obuf_data_len(in);
            if (len > 1) {
                obuf_drop(in, len - 1);
            }
            return 0;
        }

        if (off > 0) {
            obuf_drop(in, (size_t)off);
        }

        if (obuf_data_len(in) < 5) {
            return 0;
        }

        int cmd = obuf_peek(in, 2);
        int len = obuf_peek(in, 3);
        if (cmd < 0 || len < 0) {
            return 0;
        }

        /* LEN 合法性保护：防止异常长度导致解析卡死 */
        if ((uint8_t)len == 0 || (uint8_t)len > 200) {
            obuf_drop(in, 1);
            continue;
        }

        if ((uint8_t)cmd != 0x09) {
            obuf_drop(in, 1);
            continue;
        }

        size_t frame_len = (size_t)((uint8_t)len) + 5;
        if (obuf_data_len(in) < frame_len) {
            return 0;
        }

        uint8_t calc = 0;
        for (size_t i = 0; i < frame_len - 1; i++) {
            int b = obuf_peek(in, i);
            if (b < 0) return 0;
            calc ^= (uint8_t)b;
        }
        int chk = obuf_peek(in, frame_len - 1);
        if (chk < 0) return 0;
        if ((uint8_t)chk != calc) {
            obuf_drop(in, 1);
            continue;
        }

        memset(out, 0, sizeof(*out));
        out->cmd = (uint8_t)cmd;
        out->sub_cmd = (uint8_t)obuf_peek(in, 4);

        if (out->sub_cmd == 0x01 && (uint8_t)len >= 9) {
            uint8_t fraw[4];
            for (int i = 0; i < 4; i++) {
                fraw[i] = (uint8_t)obuf_peek(in, 5 + i);
            }
            memcpy(&out->f1, fraw, sizeof(float));
            for (int i = 0; i < 4; i++) {
                fraw[i] = (uint8_t)obuf_peek(in, 9 + i);
            }
            memcpy(&out->f2, fraw, sizeof(float));
            out->has_f2 = 1;
        } else if ((out->sub_cmd == 0x02 || out->sub_cmd == 0x03) && (uint8_t)len >= 6) {
            out->fid = (uint8_t)obuf_peek(in, 5);
            out->has_fid = 1;

            uint8_t fraw[4];
            for (int i = 0; i < 4; i++) {
                fraw[i] = (uint8_t)obuf_peek(in, 6 + i);
            }
            memcpy(&out->f1, fraw, sizeof(float));
            if (out->sub_cmd == 0x03) {
                out->auto_close_sec = out->f1;
            }

            int text_len = (int)((uint8_t)len) - 6;
            if (text_len > 0) {
                int cap = (int)sizeof(out->text) - 1;
                if (text_len > cap) text_len = cap;
                for (int i = 0; i < text_len; i++) {
                    int b = obuf_peek(in, 10 + (size_t)i);
                    out->text[i] = (b < 0) ? '\0' : (char)b;
                }
                out->text[text_len] = '\0';
                out->has_text = 1;
            }
        }

        obuf_drop(in, frame_len);
        return 1;
    }
    obuf_drop(in, 1);
    return 0;
}

static int is_ascii_alnum(char c)
{
    if (c >= '0' && c <= '9') return 1;
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c == '_') return 1;
    return 0;
}

static void ascii_lower_copy(const char *src, char *dst, size_t cap)
{
    if (!src || !dst || cap == 0) {
        return;
    }
    size_t i = 0;
    for (; i + 1 < cap && src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 0x80) {
            dst[i] = (char)tolower(c);
        } else {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

static int contains_token_ci(const char *s, const char *token, int word_boundary)
{
    if (!s || !token) return 0;
    char s_low[160];
    char t_low[64];
    ascii_lower_copy(s, s_low, sizeof(s_low));
    ascii_lower_copy(token, t_low, sizeof(t_low));

    const char *p = s_low;
    size_t tlen = strlen(t_low);
    if (tlen == 0) return 0;

    while ((p = strstr(p, t_low)) != NULL) {
        if (!word_boundary) {
            return 1;
        }
        char prev = (p == s_low) ? '\0' : *(p - 1);
        char next = *(p + tlen);
        if (!is_ascii_alnum(prev) && !is_ascii_alnum(next)) {
            return 1;
        }
        p += tlen;
    }
    return 0;
}

static int contains_phrase_ci(const char *s, const char *phrase)
{
    if (!s || !phrase) return 0;

    char s_low[160];
    char p_low[80];
    ascii_lower_copy(s, s_low, sizeof(s_low));
    ascii_lower_copy(phrase, p_low, sizeof(p_low));
    if (strstr(s_low, p_low) != NULL) {
        return 1;
    }

    char s_norm[160];
    char p_norm[80];
    size_t si = 0;
    for (size_t i = 0; s[i] != '\0' && si + 1 < sizeof(s_norm); i++) {
        char c = s[i];
        if (c == ' ' || c == '_') continue;
        if ((unsigned char)c < 0x80) {
            s_norm[si++] = (char)tolower((unsigned char)c);
        } else {
            s_norm[si++] = c;
        }
    }
    s_norm[si] = '\0';

    size_t pi = 0;
    for (size_t i = 0; phrase[i] != '\0' && pi + 1 < sizeof(p_norm); i++) {
        char c = phrase[i];
        if (c == ' ' || c == '_') continue;
        if ((unsigned char)c < 0x80) {
            p_norm[pi++] = (char)tolower((unsigned char)c);
        } else {
            p_norm[pi++] = c;
        }
    }
    p_norm[pi] = '\0';

    return strstr(s_norm, p_norm) != NULL;
}

typedef enum {
    FIELD_NONE = 0,
    FIELD_SYNC,
    FIELD_INC,
    FIELD_AZI,
    FIELD_GTF,
    FIELD_MTF,
    FIELD_TF
} field_kind_t;

typedef struct {
    field_kind_t kind;
    uint8_t highlight;
    const char *desc;
    const char * const *cn_keywords;
    const char * const *phrases;
    const char * const *tokens;
} field_rule_t;

static const char *const k_sync_cn[] = {"同步头", "同步", NULL};
static const char *const k_sync_tokens[] = {"fid", "sync", NULL};

static const char *const k_inc_cn[] = {"井斜角", "井斜", "倾角", NULL};
static const char *const k_inc_phrases[] = {"inclination", "deviation", "static_inc", "continue_inc", NULL};
static const char *const k_inc_tokens[] = {"inc", NULL};

static const char *const k_azi_cn[] = {"方位角", "方位", NULL};
static const char *const k_azi_phrases[] = {"azimuth angle", "azimuth", "static_azi", "continue_azi", NULL};
static const char *const k_azi_tokens[] = {"azi", NULL};

static const char *const k_gtf_cn[] = {"重力工具面", "重力高边角", "重力高边", NULL};
static const char *const k_gtf_phrases[] = {"gravity tool face", "gravity high side angle", "gravity high side", NULL};
static const char *const k_gtf_tokens[] = {"gtf", "ghsa", "ghs", NULL};

static const char *const k_mtf_cn[] = {"磁性工具面", "磁工具面", "磁性高边角", "磁高边角", "磁性高边", "磁高边", NULL};
static const char *const k_mtf_phrases[] = {"magnetic tool face", "magnetic high side angle", "magnetic high side", NULL};
static const char *const k_mtf_tokens[] = {"mtf", "mhsa", "mhs", NULL};

static const char *const k_tf_cn[] = {"工具面", NULL};
static const char *const k_tf_phrases[] = {"toolface", "tool face", NULL};

static const field_rule_t k_field_rules[] = {
    {FIELD_SYNC, 1, "sync", k_sync_cn, NULL, k_sync_tokens},
    {FIELD_GTF,  0, "gtf",  k_gtf_cn,  k_gtf_phrases, k_gtf_tokens},
    {FIELD_MTF,  0, "mtf",  k_mtf_cn,  k_mtf_phrases, k_mtf_tokens},
    {FIELD_TF,   0, "tf",   k_tf_cn,   k_tf_phrases,  NULL},
    {FIELD_INC,  0, "inc",  k_inc_cn,  k_inc_phrases, k_inc_tokens},
    {FIELD_AZI,  0, "azi",  k_azi_cn,  k_azi_phrases, k_azi_tokens},
};

static int contains_any_cn(const char *s, const char * const *list)
{
    if (!s || !list) return 0;
    for (int i = 0; list[i] != NULL; i++) {
        if (strstr(s, list[i]) != NULL) return 1;
    }
    return 0;
}

static int contains_any_phrase(const char *s, const char * const *list)
{
    if (!s || !list) return 0;
    for (int i = 0; list[i] != NULL; i++) {
        if (contains_phrase_ci(s, list[i])) return 1;
    }
    return 0;
}

static int contains_any_token(const char *s, const char * const *list)
{
    if (!s || !list) return 0;
    for (int i = 0; list[i] != NULL; i++) {
        if (contains_token_ci(s, list[i], 1)) return 1;
    }
    return 0;
}

typedef struct {
    field_kind_t kind;
    uint8_t highlight;
} field_match_t;

static field_match_t match_field_name(const char *name)
{
    field_match_t m = {FIELD_NONE, 0};
    if (!name) return m;

    for (size_t i = 0; i < (sizeof(k_field_rules) / sizeof(k_field_rules[0])); i++) {
        const field_rule_t *r = &k_field_rules[i];
        if (contains_any_cn(name, r->cn_keywords) ||
            contains_any_phrase(name, r->phrases) ||
            contains_any_token(name, r->tokens)) {
            m.kind = r->kind;
            m.highlight = r->highlight;
            return m;
        }
    }
    return m;
}

static const char *field_display_name(field_kind_t kind, const char *raw, char *buf, size_t cap)
{
    if (!buf || cap == 0) {
        return raw ? raw : "";
    }

    switch (kind) {
    case FIELD_INC:
        snprintf(buf, cap, "井斜 Inc");
        return buf;
    case FIELD_AZI:
        snprintf(buf, cap, "方位 Azi");
        return buf;
    case FIELD_GTF:
        snprintf(buf, cap, "重力高边角 GTF");
        return buf;
    case FIELD_MTF:
        snprintf(buf, cap, "磁性高边角 MTF");
        return buf;
    case FIELD_TF:
        snprintf(buf, cap, "工具面 TF");
        return buf;
    case FIELD_SYNC:
        snprintf(buf, cap, "同步头 Sync");
        return buf;
    default:
        break;
    }

    if (raw && raw[0] != '\0') {
        return raw;
    }
    return "";
}

void app_init(lv_disp_t *disp)
{
    (void)disp;
    
    // 创建仪表盘界面
    lv_obj_t *scr = dashboard_create();
    lv_scr_load(scr);

    /* 初始化协议接收缓冲区（与板端一致） */
    obuf_init(&g_rx_buf, g_rx_storage, sizeof(g_rx_storage));

    /* 启动模拟定时器：用 0x09 新协议模拟字节流 */
    lv_timer_create(sim_timer_cb, 200, NULL);
}

void app_stop_sim(void)
{
    // 如果有需要，可以保存 timer handle 并在此时删除
}

// 模拟数据生成器
static void sim_timer_cb(lv_timer_t *t)
{
    (void)t;

    static uint32_t tick = 0;
    tick++;

    /* 模拟串口连接状态 */
    strncpy(g_sim.port_name, "SIM", sizeof(g_sim.port_name));
    g_sim.port_connected = 1;

    /* 1) 生成泵压帧 */
    float pump_a = 15.0f + (float)(tick % 10) * 0.1f;
    float pump_b = 0.5f;
    uint8_t frame_buf[256];
    size_t f_len = sx_build_frame_pump(pump_a, pump_b, frame_buf, sizeof(frame_buf));
    if (f_len > 0) {
        obuf_write_frag(&g_rx_buf, frame_buf, f_len);
    }

    /* 2) 生成“参数名+值”帧（覆盖中英文/缩写别名） */
    static const char *k_names[] = {
        "井斜", "inclination", "inc",
        "方位", "azimuth", "azi",
        "重力工具面", "gtf",
        "磁性工具面", "mtf",
        "工具面", "toolface",
        "同步头", "sync"
    };
    const size_t name_cnt = sizeof(k_names) / sizeof(k_names[0]);
    const char *name = k_names[tick % name_cnt];
    float value = (float)((tick * 7) % 360);
    f_len = sx_build_frame_name(value, name, frame_buf, sizeof(frame_buf));
    if (f_len > 0) {
        obuf_write_frag(&g_rx_buf, frame_buf, f_len);
    }

    /* 3) 解析字节流并更新 UI 数据（与板端一致） */
    sx_frame_t frame;
    int process_cnt = 0;
    while (process_cnt < 50 && sx_try_parse_one(&g_rx_buf, &frame)) {
        process_cnt++;

        if (frame.sub_cmd == 0x01 && frame.has_f2) {
            float press = (frame.f1 > 0.0f) ? frame.f1 : frame.f2;
            g_sim.pump_pressure = press;
            g_sim.pump_status = (press > 2.0f) ? 1 : 0;
        } else if (frame.sub_cmd == 0x02) {
            const char *fname = frame.has_text ? frame.text : "";
            field_match_t match = match_field_name(fname);
            char disp_name[32];
            const char *show_name = field_display_name(match.kind, fname, disp_name, sizeof(disp_name));
            dashboard_append_decode_row(show_name, frame.f1, match.highlight);

            if (match.kind == FIELD_INC) {
                g_sim.inclination = frame.f1;
            } else if (match.kind == FIELD_AZI) {
                g_sim.azimuth = frame.f1;
            } else if (match.kind == FIELD_GTF) {
                g_sim.toolface = frame.f1;
                g_sim.tf_type = 0x13;
            } else if (match.kind == FIELD_MTF) {
                g_sim.toolface = frame.f1;
                g_sim.tf_type = 0x14;
            } else if (match.kind == FIELD_TF) {
                g_sim.toolface = frame.f1;
            }

            if (match.kind == FIELD_TF || match.kind == FIELD_GTF || match.kind == FIELD_MTF) {
                for (int i = 0; i < 4; i++) {
                    g_sim.toolface_history[i] = g_sim.toolface_history[i + 1];
                }
                g_sim.toolface_history[4] = g_sim.toolface;
            }
        }
    }

    if (process_cnt > 0) {
        dashboard_update(&g_sim);
    }
}
