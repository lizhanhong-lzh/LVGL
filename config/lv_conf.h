#ifndef LV_CONF_H
#define LV_CONF_H

/* Minimal LVGL configuration for PC simulator.
 * LVGL will provide defaults for many options via lv_conf_internal.h.
 */

/* Color depth: SDL2 on PC typically uses 32-bit */
#define LV_COLOR_DEPTH 32

/* Use standard C library */
#define LV_USE_STDLIB_MALLOC 1
#define LV_USE_STDLIB_STRING 1
#define LV_USE_STDLIB_SPRINTF 1

/* Enable a few widgets we use */
#define LV_USE_LABEL 1
#define LV_USE_BAR 1
#define LV_USE_ARC 1
#define LV_USE_CHART 1
#define LV_USE_LED 1

/* Fonts */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_SIMSUN_16_CJK 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Logging (optional) */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* Performance */
#define LV_USE_PERF_MONITOR 0

#endif /*LV_CONF_H*/
