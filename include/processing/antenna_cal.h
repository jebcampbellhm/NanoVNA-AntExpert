#ifndef ANTENNA_CAL_H
#define ANTENNA_CAL_H
#include "processing/antenna_cal_grid.h"
#ifdef NANOVNA_F303
bool antenna_cal_valid(const properties_t *p);
bool antenna_cal_saved(unsigned profile);
bool antenna_cal_load(unsigned profile);
bool antenna_cal_interpolate(freq_t f, float data[CAL_TYPE_COUNT][2]);
/* 0 selects profiles; 1/2/3 collect OPEN/SHORT/LOAD; 4 commits. */
bool antenna_cal_begin(unsigned mask);
bool antenna_cal_step(void);
void antenna_cal_cancel(void);
unsigned antenna_cal_stage(void);
extern uint8_t antenna_cal_capture; /* 0: normal sweep; profile+1 while collecting */
#endif
#endif
