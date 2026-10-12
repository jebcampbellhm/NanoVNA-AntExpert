#ifndef NANOVNA_ANTENNA_H
#define NANOVNA_ANTENNA_H
#include <stdbool.h>
#include <stdint.h>

#define ANTENNA_BAND_COUNT 16
#define ANTENNA_HF_COUNT 11
#define ANTENNA_60M_BAND 2
#define ANTENNA_60M_CHANNELS 5
extern const uint32_t antenna_60m_centers[ANTENNA_60M_CHANNELS];
typedef struct { const char *name; uint32_t start, stop; } antenna_band_t;
extern const antenna_band_t antenna_bands[ANTENNA_BAND_COUNT];
/* Negative means invalid; 999 means an open/short or SWR beyond the display. */
float antenna_swr(float re, float im);
typedef struct {
  bool valid, bandwidth_valid, left_limited, right_limited;
  uint16_t minimum;
  float swr, low_index, high_index;
} antenna_result_t;
antenna_result_t antenna_analyze(const float *swr, unsigned count);
uint32_t antenna_favorites_decode(const uint8_t bytes[3]);
void antenna_favorites_encode(uint8_t bytes[3], uint32_t mask);
#endif
