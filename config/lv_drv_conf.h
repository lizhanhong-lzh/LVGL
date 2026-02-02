#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

/* lv_drivers configuration (PC simulator) */

/* Use the unified SDL driver (display + mouse + mousewheel + keyboard).
 * The older MONITOR/MOUSE/KEYBOARD drivers are deprecated in lv_drivers v8.x. */
#define USE_SDL         1
#define USE_MONITOR     0
#define USE_MOUSE       0
#define USE_KEYBOARD    0
#define USE_MOUSEWHEEL  0

/* Default monitor resolution */
#define MONITOR_HOR_RES 1200
#define MONITOR_VER_RES 800

/* lv_drivers/sdl/sdl.c uses `#include SDL_INCLUDE_PATH`.
 * When USE_MONITOR=1 it maps SDL_INCLUDE_PATH to MONITOR_SDL_INCLUDE_PATH.
 * In our PC simulator build, SDL2 headers are exposed as <SDL.h>. */
#define SDL_INCLUDE_PATH <SDL.h>
#define MONITOR_SDL_INCLUDE_PATH SDL_INCLUDE_PATH

/* SDL driver resolution (required when USE_SDL=1) */
#define SDL_HOR_RES MONITOR_HOR_RES
#define SDL_VER_RES MONITOR_VER_RES

/* Optional but explicit defaults */
#define SDL_ZOOM 1
#define SDL_DOUBLE_BUFFERED 0
#define SDL_DUAL_DISPLAY 0
#define SDL_VIRTUAL_MACHINE 0

/* Keep MONITOR_* values defined (even when USE_MONITOR=0) for compatibility */
#define MONITOR_ZOOM SDL_ZOOM
#define MONITOR_DOUBLE_BUFFERED SDL_DOUBLE_BUFFERED
#define MONITOR_DUAL SDL_DUAL_DISPLAY
#define MONITOR_VIRTUAL_MACHINE SDL_VIRTUAL_MACHINE

#endif /*LV_DRV_CONF_H*/
