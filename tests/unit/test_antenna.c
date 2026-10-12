#include "processing/antenna.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void near(float a, float b) { assert(fabsf(a-b) < 0.0001f); }
int main(void) {
  near(antenna_swr(0,0),1);
  near(antenna_swr(1.0f/3,0),2);
  near(antenna_swr(0,-0.5f),3);
  assert(antenna_swr(1,0) == 999);
  assert(antenna_swr(-1,0) == 999);
  assert(antenna_swr(0.9f,0.9f) == 999);
  uint32_t bits = 0x7fc00000u; float bad;
  memcpy(&bad,&bits,4);
  assert(antenna_swr(bad,0) < 0);
  bits = 0x7f800000u; memcpy(&bad,&bits,4);
  assert(antenna_swr(0,bad) < 0);
  float a[] = {4,3,1.5f,1,1.5f,3,1.1f,1.5f};
  antenna_result_t r = antenna_analyze(a,8);
  assert(r.valid && r.minimum == 3 && r.bandwidth_valid);
  near(r.low_index,1.6666667f); near(r.high_index,4.3333333f);
  assert(!r.left_limited && !r.right_limited); // Separate dip excluded.
  float edge[] = {1.2f,1,1.1f};
  r = antenna_analyze(edge,3);
  assert(r.bandwidth_valid && r.left_limited && r.right_limited);
  near(r.low_index,0); near(r.high_index,2);
  float missing[] = {3,-1,1,3};
  r = antenna_analyze(missing,4);
  assert(r.valid && !r.bandwidth_valid);
  float high[] = {999,999};
  r = antenna_analyze(high,2);
  assert(r.valid && !r.bandwidth_valid);
  assert(!antenna_analyze(NULL,4).valid);
  assert(!antenna_analyze(a,0).valid);
  assert(!antenna_analyze(a,65536).valid);
  float invalid[] = {-1,-1};
  assert(!antenna_analyze(invalid,2).valid);
  assert(!antenna_analyze(edge,1).bandwidth_valid);
  uint8_t bytes[3] = {0};
  assert(antenna_favorites_decode(bytes) != 0);
  for (unsigned i=0; i<ANTENNA_BAND_COUNT; ++i) {
    antenna_favorites_encode(bytes,1u<<i);
    assert(antenna_favorites_decode(bytes) == (1u<<i));
    assert(antenna_bands[i].start < antenna_bands[i].stop);
    assert(antenna_bands[i].stop <= 1300000000);
  }
  antenna_favorites_encode(bytes,0);
  assert(antenna_favorites_decode(bytes) == 0); // Empty is distinct from uninitialized.
  antenna_favorites_encode(bytes,0xffffffff);
  assert(antenna_favorites_decode(bytes) == 0xfffff);
  puts("antenna tests passed");
  return 0;
}
