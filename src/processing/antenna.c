#include "processing/antenna.h"
#include <math.h>
#include <string.h>

/* US-BandPlan_3-2024.pdf. The 60 m measurement span includes gaps between
 * the five channel windows; markers identify their centers. */
const uint32_t antenna_60m_centers[ANTENNA_60M_CHANNELS] = {
  5332000, 5348000, 5358500, 5373000, 5405000
};
const antenna_band_t antenna_bands[ANTENNA_BAND_COUNT] = {
  {"160m",1800000,2000000,0}, {"80/75m",3500000,4000000,3600000},
  {"60m",5330600,5406400,0}, {"40m",7000000,7300000,7125000},
  {"30m",10100000,10150000,10150000}, {"20m",14000000,14350000,14150000},
  {"17m",18068000,18168000,18110000}, {"15m",21000000,21450000,21200000},
  {"12m",24890000,24990000,24930000}, {"10m",28000000,29700000,28300000},
  {"6m",50000000,54000000,0}, {"2m",144000000,148000000,0},
  {"1.25m",222000000,225000000,0}, {"70cm",420000000,450000000,0},
  {"33cm",902000000,928000000,0}, {"23cm",1240000000,1300000000,0},
  {"EU868",863000000,870000000,0}
};

static bool finite_float(float x) {
  uint32_t bits;
  memcpy(&bits, &x, sizeof(bits));
  return (bits & 0x7f800000u) != 0x7f800000u; // Also under -ffast-math.
}
float antenna_swr(float re, float im) {
  if (!finite_float(re) || !finite_float(im)) return -1;
  if (fabsf(re) >= 1 || fabsf(im) >= 1) return 999;
  float rho = sqrtf(re * re + im * im);
  if (rho >= 0.998f) return 999;
  return (1 + rho) / (1 - rho);
}
antenna_result_t antenna_analyze(const float *swr, unsigned count) {
  antenna_result_t r = {0};
  if (!swr || !count || count > UINT16_MAX) return r;
  r.swr = 1000;
  for (unsigned i = 0; i < count; ++i) {
    if (finite_float(swr[i]) && swr[i] >= 1 && swr[i] < r.swr) {
      r.valid = true; r.swr = swr[i]; r.minimum = i;
    }
  }
  if (!r.valid || r.swr > 2 || count < 2) return r;
  unsigned lo = r.minimum, hi = lo;
  while (lo && finite_float(swr[lo-1]) && swr[lo-1] >= 1 && swr[lo-1] <= 2) --lo;
  while (hi + 1 < count && finite_float(swr[hi+1]) && swr[hi+1] >= 1 && swr[hi+1] <= 2) ++hi;
  r.low_index = lo; r.high_index = hi;
  r.left_limited = lo == 0; r.right_limited = hi + 1 == count;
  // Missing samples do not constitute a measured crossing.
  if ((!r.left_limited && (!finite_float(swr[lo-1]) || swr[lo-1] < 1)) ||
      (!r.right_limited && (!finite_float(swr[hi+1]) || swr[hi+1] < 1))) return r;
  if (!r.left_limited) r.low_index -= (2 - swr[lo]) / (swr[lo-1] - swr[lo]);
  if (!r.right_limited) r.high_index += (2 - swr[hi]) / (swr[hi+1] - swr[hi]);
  r.bandwidth_valid = true;
  return r;
}
// v2: 16 favorite bits, five view bits (0..15 band, 16 MULTI, 17 CABLE, 18 EU868), one
// startup-mode bit and a two-bit tag. Invalid/erased records use defaults.
static bool saved_view_valid(const uint8_t bytes[3]) {
  return (bytes[2] & 0xc0) == 0xc0 && (bytes[2] & 31) <= ANTENNA_VIEW_EU868;
}
unsigned antenna_view_decode(const uint8_t bytes[3]) {
  return saved_view_valid(bytes) ? bytes[2] & 31 : 5;
}
uint32_t antenna_favorites_decode(const uint8_t bytes[3]) {
  if (saved_view_valid(bytes)) return bytes[0] | ((uint32_t)bytes[1]<<8);
  if ((bytes[2] & 0xe0) != 0xa0) return (1u<<3) | (1u<<5) | (1u<<10) | (1u<<11) | (1u<<13);
  uint32_t stored = bytes[0] | ((uint32_t)bytes[1]<<8) | ((uint32_t)(bytes[2]&15)<<16);
  // Migrate legacy favorites, including the five old 60 m entries.
  return (stored & 3u) | ((stored & 0x7cu) ? 4u : 0u) | ((stored >> 4) & 0xfff8u);
}
bool antenna_mode_decode(const uint8_t bytes[3]) {
  return saved_view_valid(bytes) ? (bytes[2] & 0x20) != 0 : (bytes[2] & 0xf0) == 0xb0;
}
static void encode_settings(uint8_t bytes[3], uint32_t favorites, unsigned view, bool enabled) {
  bytes[0] = favorites; bytes[1] = favorites >> 8;
  bytes[2] = 0xc0 | view | (enabled ? 0x20 : 0);
}
void antenna_favorites_encode(uint8_t bytes[3], uint32_t mask) {
  encode_settings(bytes,mask,antenna_view_decode(bytes),antenna_mode_decode(bytes));
}
void antenna_mode_encode(uint8_t bytes[3], bool enabled) {
  encode_settings(bytes,antenna_favorites_decode(bytes),antenna_view_decode(bytes),enabled);
}
void antenna_view_encode(uint8_t bytes[3], unsigned view) {
  if (view > ANTENNA_VIEW_EU868) return;
  encode_settings(bytes,antenna_favorites_decode(bytes),view,antenna_mode_decode(bytes));
}
