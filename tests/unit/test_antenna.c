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
  const uint32_t cw_data_ends[ANTENNA_BAND_COUNT] = {
    0,3600000,0,7125000,10150000,14150000,18110000,21200000,
    24930000,28300000,0,0,0,0,0,0
  };
  assert(antenna_favorites_decode(bytes) != 0);
  for (unsigned i=0; i<ANTENNA_BAND_COUNT; ++i) {
    if (i < 16) {
      antenna_favorites_encode(bytes,1u<<i);
      assert(antenna_favorites_decode(bytes) == (1u<<i));
    }
    assert(antenna_bands[i].start < antenna_bands[i].stop);
    assert(antenna_bands[i].stop <= 1300000000);
    assert(antenna_bands[i].cw_data_stop == cw_data_ends[i]);
    if (cw_data_ends[i]) {
      assert(cw_data_ends[i] > antenna_bands[i].start);
      assert(cw_data_ends[i] <= antenna_bands[i].stop);
    }
  }
  antenna_favorites_encode(bytes,0);
  assert(antenna_favorites_decode(bytes) == 0); // Empty is distinct from uninitialized.
  antenna_favorites_encode(bytes,0xffffffff);
  assert(antenna_favorites_decode(bytes) == 0xffff);
  // Every legacy favorite bit keeps its band identity after collapsing 60 m.
  for (unsigned i=0; i<20; ++i) {
    uint32_t old = 1u<<i;
    bytes[0]=old; bytes[1]=old>>8; bytes[2]=0xa0|((old>>16)&15);
    unsigned index = i<2 ? i : (i<7 ? 2 : i-4);
    assert(antenna_favorites_decode(bytes)==(1u<<index));
  }
  antenna_favorites_encode(bytes,1u<<ANTENNA_60M_BAND);
  assert(bytes[0]==4 && bytes[1]==0 && bytes[2]==0xc5);
  assert(!antenna_mode_decode(bytes)); // Legacy favorites boot into VNA.
  antenna_mode_encode(bytes,true);
  assert(antenna_mode_decode(bytes) && antenna_favorites_decode(bytes)==4);
  antenna_favorites_encode(bytes,1u<<13);
  assert(antenna_mode_decode(bytes) && antenna_favorites_decode(bytes)==(1u<<13));
  antenna_mode_encode(bytes,false);
  assert(!antenna_mode_decode(bytes) && antenna_favorites_decode(bytes)==(1u<<13));
  memset(bytes,0xff,3);
  assert(!antenna_mode_decode(bytes));
  uint32_t defaults=antenna_favorites_decode(bytes);
  antenna_mode_encode(bytes,true);
  assert(antenna_mode_decode(bytes) && antenna_favorites_decode(bytes)==defaults);
  antenna_favorites_encode(bytes,0);
  assert(antenna_mode_decode(bytes) && antenna_favorites_decode(bytes)==0);
  assert(antenna_bands[2].start==5330600 && antenna_bands[2].stop==5406400);
  for(unsigned i=0;i<ANTENNA_60M_CHANNELS;i++) {
    assert(antenna_60m_centers[i]>antenna_bands[2].start);
    assert(antenna_60m_centers[i]<antenna_bands[2].stop);
    if(i) assert(antenna_60m_centers[i]>antenna_60m_centers[i-1]);
  }
  // Every band and MULTI survive encoding, favorites edits, and ANT/VNA changes.
  for(unsigned view=0;view<=ANTENNA_VIEW_EU868;++view) {
    antenna_view_encode(bytes,view);
    assert(antenna_view_decode(bytes)==view);
    antenna_favorites_encode(bytes,0xa55a);
    antenna_mode_encode(bytes,true);
    assert(antenna_view_decode(bytes)==view && antenna_mode_decode(bytes));
    antenna_mode_encode(bytes,false);
    assert(antenna_view_decode(bytes)==view && !antenna_mode_decode(bytes));
    assert(antenna_favorites_decode(bytes)==0xa55a);
  }
  uint8_t previous[3]; memcpy(previous,bytes,3);
  antenna_view_encode(bytes,19); assert(memcmp(previous,bytes,3)==0);
  for(unsigned x=0;x<256;++x) {
    bytes[0]=bytes[1]=0xff;bytes[2]=x;
    assert(antenna_view_decode(bytes)<=ANTENNA_VIEW_EU868);
    if((x&0xc0)==0xc0 && (x&31)>18) {
      assert(antenna_view_decode(bytes)==5 && !antenna_mode_decode(bytes));
    }
  }
  // Both legacy formats retain their favorites/mode and default to 20 m.
  for(unsigned mode=0;mode<2;++mode) for(unsigned i=0;i<20;++i) {
    uint32_t old=1u<<i;bytes[0]=old;bytes[1]=old>>8;
    bytes[2]=0xa0|(mode?0x10:0)|((old>>16)&15);
    uint32_t favorites=antenna_favorites_decode(bytes);
    assert(antenna_view_decode(bytes)==5);
    antenna_view_encode(bytes,16);
    assert(antenna_view_decode(bytes)==16 && antenna_mode_decode(bytes)==(mode!=0));
    assert(antenna_favorites_decode(bytes)==favorites);
  }
  puts("antenna tests passed");
  return 0;
}
