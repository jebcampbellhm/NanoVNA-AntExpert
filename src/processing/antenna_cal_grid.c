#include "processing/antenna_cal_grid.h"
#include "processing/antenna.h"
#pragma GCC optimize("Os")

/* ANT1 storage layout is unchanged. This private-fork grid update requires
 * recalibrating V/U ANT; existing V/U coefficients are not auto-rejected.
 * HF is byte-for-byte unchanged from AntExpert 0.1.0.
 * HF spacing is at most 10 kHz; the eight surplus points go to 10 m.
 * Each band includes its endpoints. Gaps are deliberately not calibrated. */
// Share preset endpoints with the UI; do not store a second frequency table.
// These counts and the shared endpoints define the persisted ANT1 grid.
static const uint8_t points[ANTENNA_BAND_COUNT] = {
  21,51,9,31,6,36,11,46,11,179,13,13,10,89,77,177,22
};
// UI IDs stay stable; the calibration grid must remain frequency-ordered.
static unsigned segment(unsigned s) { return s < 14 ? s : s == 14 ? ANTENNA_EU868_BAND : s-1; }
static unsigned first(unsigned p) { return p ? 10 : 0; }
static unsigned end(unsigned p) { return p ? ANTENNA_BAND_COUNT : 10; }
static uint32_t at(unsigned s, unsigned i) {
  const antenna_band_t *b = &antenna_bands[s];
  unsigned intervals = points[s]-1;
  return b->start + ((uint64_t)(b->stop-b->start)*i + intervals/2)/intervals;
}
uint32_t antenna_cal_frequency(unsigned p, unsigned i) {
  if (p > 1 || i >= ANT_CAL_POINTS) return 0;
  for (unsigned s=first(p); s<end(p); ++s) {
    unsigned b=segment(s);
    if (i < points[b]) return at(b,i);
    i -= points[b];
  }
  return 0;
}
bool antenna_cal_range(unsigned p, uint32_t a, uint32_t b) {
  if (p > 1 || a > b) return false;
  for (unsigned s=first(p); s<end(p); ++s) {
    const antenna_band_t *g=&antenna_bands[segment(s)];
    if (a >= g->start && b <= g->stop) return true;
  }
  return false;
}
bool antenna_cal_bracket(unsigned p, uint32_t f, unsigned *index, float *fraction) {
  if (p > 1) return false;
  unsigned base=0;
  for (unsigned s=first(p); s<end(p); ++s) {
    unsigned b=segment(s);
    const antenna_band_t *g=&antenna_bands[b];
    if (f < g->start || f > g->stop) { base+=points[b]; continue; }
    unsigned lo=0, hi=points[b]-1;
    while (hi-lo>1) {
      unsigned mid=(lo+hi)/2;
      if (at(b,mid)<=f) lo=mid; else hi=mid;
    }
    uint32_t a=at(b,lo), z=at(b,hi);
    *index=base+lo; *fraction=(float)(f-a)/(z-a);
    return true;
  }
  return false;
}
