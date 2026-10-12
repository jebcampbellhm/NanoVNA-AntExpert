#ifndef NANOVNA_ANTENNA_UI_H
#define NANOVNA_ANTENNA_UI_H
#include <stdbool.h>
#include <stdint.h>
#ifdef NANOVNA_F303
void antenna_ui_enter(uint16_t data);
void antenna_ui_restore(void);
bool antenna_ui_draw(void);
bool antenna_ui_touch(int x, int y);
bool antenna_ui_lever(uint16_t status);
void antenna_ui_measurement(void);
void antenna_ui_prepare_sweep(void);
#else
static inline void antenna_ui_restore(void) {}
static inline bool antenna_ui_draw(void) { return false; }
static inline bool antenna_ui_touch(int x, int y) { (void)x; (void)y; return false; }
static inline bool antenna_ui_lever(uint16_t s) { (void)s; return false; }
static inline void antenna_ui_measurement(void) {}
static inline void antenna_ui_prepare_sweep(void) {}
#endif
#endif
