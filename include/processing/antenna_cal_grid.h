#ifndef ANTENNA_CAL_GRID_H
#define ANTENNA_CAL_GRID_H
#include <stdint.h>
#include <stdbool.h>
#define ANT_CAL_POINTS 401
#define ANT_CAL_MAGIC 0x414e5431u /* ANT1: distinct from legacy uniform calibration */
#define ANT_CAL_FIRST_SLOT 5
#define ANT_CAL_SCRATCH 0x08039000u
#define ANT_CAL_PLANE_BYTES 4096u
uint32_t antenna_cal_frequency(unsigned profile, unsigned index);
bool antenna_cal_bracket(unsigned profile, uint32_t frequency, unsigned *index, float *fraction);
bool antenna_cal_range(unsigned profile, uint32_t start, uint32_t stop);
#endif
