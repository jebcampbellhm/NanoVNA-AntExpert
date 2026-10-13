#include "processing/antenna_cal_grid.h"
#include "processing/antenna.h"
#pragma GCC optimize("Os")

/* Published ANT1 grid used by AntExpert 0.1.0 and later.
 * Changes to point counts or shared band endpoints require grid versioning.
 * HF spacing is at most 10 kHz; the eight surplus points go to 10 m.
 * Each band includes its endpoints. Gaps are deliberately not calibrated. */
// Share preset endpoints with the UI; do not store a second frequency table.
// These counts and the shared endpoints define the persisted ANT1 grid.
static const uint8_t points[ANTENNA_BAND_COUNT] = {
  21,51,9,31,6,36,11,46,11,179,61,91,41,101,46,61
};
static unsigned first(unsigned p) { return p ? 10 : 0; }
static unsigned end(unsigned p) { return p ? 16 : 10; }
static uint32_t at(unsigned s, unsigned i) {
  const antenna_band_t *b = &antenna_bands[s];
  unsigned intervals = points[s]-1;
  return b->start + ((uint64_t)(b->stop-b->start)*i + intervals/2)/intervals;
}
uint32_t antenna_cal_frequency(unsigned p, unsigned i) {
  if (p > 1 || i >= ANT_CAL_POINTS) return 0;
  for (unsigned s=first(p); s<end(p); ++s) {
    if (i < points[s]) return at(s,i);
    i -= points[s];
  }
  return 0;
}
bool antenna_cal_range(unsigned p, uint32_t a, uint32_t b) {
  if (p > 1 || a > b) return false;
  for (unsigned s=first(p); s<end(p); ++s)
    if (a >= antenna_bands[s].start && b <= antenna_bands[s].stop) return true;
  return false;
}
bool antenna_cal_bracket(unsigned p, uint32_t f, unsigned *index, float *fraction) {
  if (p > 1) return false;
  unsigned base=0;
  for (unsigned s=first(p); s<end(p); base+=points[s++]) {
    const antenna_band_t *g=&antenna_bands[s];
    if (f < g->start || f > g->stop) continue;
    unsigned lo=0, hi=points[s]-1;
    while (hi-lo>1) {
      unsigned mid=(lo+hi)/2;
      if (at(s,mid)<=f) lo=mid; else hi=mid;
    }
    uint32_t a=at(s,lo), b=at(s,hi);
    *index=base+lo; *fraction=(float)(f-a)/(b-a);
    return true;
  }
  return false;
}
