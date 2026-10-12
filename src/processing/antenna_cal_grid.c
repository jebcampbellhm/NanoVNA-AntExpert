#include "processing/antenna_cal_grid.h"
#pragma GCC optimize("Os")

/* ANT1 development grid: existing HF profiles require manual recalibration
 * after this allocation change. Future published grid changes need versioning.
 * HF spacing is at most 10 kHz; the eight surplus points go to 10 m.
 * Each band includes its endpoints. Gaps are deliberately not calibrated. */
typedef struct { uint32_t start, stop; uint16_t count; } segment_t;
static const segment_t grid[] = {
  {1800000,2000000,21}, {3500000,4000000,51}, {5330600,5406400,9},
  {7000000,7300000,31}, {10100000,10150000,6}, {14000000,14350000,36},
  {18068000,18168000,11}, {21000000,21450000,46}, {24890000,24990000,11},
  {28000000,29700000,179},
  {50000000,54000000,61}, {144000000,148000000,91}, {222000000,225000000,41},
  {420000000,450000000,101}, {902000000,928000000,46}, {1240000000,1300000000,61}
};
static unsigned first(unsigned p) { return p ? 10 : 0; }
static unsigned end(unsigned p) { return p ? 16 : 10; }
static uint32_t at(const segment_t *s, unsigned i) {
  return s->start + ((uint64_t)(s->stop-s->start)*i + (s->count-1)/2)/(s->count-1);
}
uint32_t antenna_cal_frequency(unsigned p, unsigned i) {
  if (p > 1 || i >= ANT_CAL_POINTS) return 0;
  for (unsigned s=first(p); s<end(p); ++s) {
    if (i < grid[s].count) return at(&grid[s],i);
    i -= grid[s].count;
  }
  return 0;
}
bool antenna_cal_range(unsigned p, uint32_t a, uint32_t b) {
  if (p > 1 || a > b) return false;
  for (unsigned s=first(p); s<end(p); ++s)
    if (a >= grid[s].start && b <= grid[s].stop) return true;
  return false;
}
bool antenna_cal_bracket(unsigned p, uint32_t f, unsigned *index, float *fraction) {
  if (p > 1) return false;
  unsigned base=0;
  for (unsigned s=first(p); s<end(p); base+=grid[s++].count) {
    const segment_t *g=&grid[s];
    if (f < g->start || f > g->stop) continue;
    unsigned lo=0, hi=g->count-1;
    while (hi-lo>1) {
      unsigned mid=(lo+hi)/2;
      if (at(g,mid)<=f) lo=mid; else hi=mid;
    }
    uint32_t a=at(g,lo), b=at(g,hi);
    *index=base+lo; *fraction=(float)(f-a)/(b-a);
    return true;
  }
  return false;
}
