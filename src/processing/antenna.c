#include "processing/antenna.h"
#include <math.h>
#include <string.h>

/* US-BandPlan_3-2024.pdf. The 60 m measurement span includes gaps between
 * the five channel windows; markers identify their centers. */
const uint32_t antenna_60m_centers[ANTENNA_60M_CHANNELS] = {
  5332000, 5348000, 5358500, 5373000, 5405000
};
const antenna_band_t antenna_bands[ANTENNA_BAND_COUNT] = {
  {"160m",1800000,2000000}, {"80/75m",3500000,4000000},
  {"60m",5330600,5406400}, {"40m",7000000,7300000},
  {"30m",10100000,10150000}, {"20m",14000000,14350000},
  {"17m",18068000,18168000}, {"15m",21000000,21450000},
  {"12m",24890000,24990000}, {"10m",28000000,29700000},
  {"6m",50000000,54000000}, {"2m",144000000,148000000},
  {"1.25m",222000000,225000000}, {"70cm",420000000,450000000},
  {"33cm",902000000,928000000}, {"23cm",1240000000,1300000000}
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
uint32_t antenna_favorites_decode(const uint8_t bytes[3]) {
  if ((bytes[2] & 0xf0) != 0xa0) return (1u<<3) | (1u<<5) | (1u<<10) | (1u<<11) | (1u<<13);
  uint32_t stored = bytes[0] | ((uint32_t)bytes[1]<<8) | ((uint32_t)(bytes[2]&15)<<16);
  // Keep the original on-flash bit positions. Any old 60 m favorite selects
  // the combined entry; all bands above it retain their original identity.
  return (stored & 3u) | ((stored & 0x7cu) ? 4u : 0u) | ((stored >> 4) & 0xfff8u);
}
void antenna_favorites_encode(uint8_t bytes[3], uint32_t mask) {
  uint32_t stored = (mask & 3u) | ((mask & 4u) ? 0x7cu : 0u) | ((mask & 0xfff8u) << 4);
  bytes[0] = stored; bytes[1] = stored >> 8; bytes[2] = 0xa0 | ((stored>>16)&15);
}
