#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t temperature_c;
    int16_t pressure_kpa;
    int16_t rpm;
    uint32_t output_per_hour;
    uint8_t online;
} plant_metrics_t;

void data_sim_init(void);
plant_metrics_t data_sim_get(void);

#ifdef __cplusplus
}
#endif
