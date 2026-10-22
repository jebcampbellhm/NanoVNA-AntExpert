"""Exercise ANT grids, real OSL math, walkthrough and flash-service write guards."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define NANOVNA_F303 1
#define SWEEP_POINTS_MAX 401
#define CAL_TYPE_COUNT 5
#define CAL_LOAD 0
#define CAL_OPEN 1
#define CAL_SHORT 2
#define CAL_THRU 3
#define CAL_ISOLN 4
#define ETERM_ED 0
#define ETERM_ES 1
#define ETERM_ER 2
#define ETERM_ET 3
#define ETERM_EX 4
#define CALSTAT_LOAD 1
#define CALSTAT_OPEN 2
#define CALSTAT_SHORT 4
#define CALSTAT_THRU 8
#define CALSTAT_ISOLN 16
#define CALSTAT_ED CALSTAT_LOAD
#define CALSTAT_EX CALSTAT_ISOLN
#define CALSTAT_ES 32
#define CALSTAT_ER 64
#define CALSTAT_ET 128
#define CALSTAT_APPLY 256
#define CALSTAT_INTERPOLATED 512
#define CALSTAT_ENHANCED_RESPONSE 1024
#define SWEEP_CH0_MEASURE 1
#define SWEEP_CH1_MEASURE 2
#define SWEEP_ENABLE 1
#define SWEEP_ONCE 2
#define BANDWIDTH_100 10
#define REDRAW_BACKUP 1
#define REDRAW_CAL_STATUS 2
#define SAVEAREA_MAX 7
#define SAVE_PROP_CONFIG_SIZE 16384
#define SAVE_CONFIG_SIZE 2048
#define SAVE_PROP_CONFIG_ADDR 0x0801c800u
#define SAVE_CONFIG_ADDR 0x08038800u
#define SAVE_FULL_AREA_SIZE (7*16384+2048)
#define NO_SAVE_SLOT 255
#define PROPERTIES_MAGIC 0x434f4e54u
#define CONFIG_MAGIC 0x12345678u
#define ARRAY_COUNT(a) (sizeof(a)/sizeof(a[0]))
#define MSG_OK 0
#define MS2ST(x) (x)
typedef uint32_t freq_t;
typedef int semaphore_t;
typedef int msg_t;
#define FREQUENCY_MIN 50000u
#define FREQUENCY_MAX 2700000000u
#define TRACES_MAX 4
#define MARKERS_MAX 4
#define MAX_TRACE_TYPE 30
#define MS_END 13
#define LM_EDELAY 4
typedef struct { unsigned type,channel,smith_format; float scale,refpos; } trace_t;
typedef struct { uint16_t index; } marker_t;
typedef struct { uint32_t magic; freq_t _frequency0,_frequency1,_cal_frequency0,_cal_frequency1;
  uint16_t _sweep_points,_cal_sweep_points,_cal_status;
  uint8_t _power,_cal_power; uint32_t _reserved1[5]; float _cal_load_r,_portz;
  int8_t _current_trace,_active_marker,_previous_marker;
  trace_t _trace[TRACES_MAX]; marker_t _markers[MARKERS_MAX];
  float _cal_data[5][401][2]; uint32_t checksum;
} properties_t;
typedef struct { uint32_t magic,_harmonic_freq_threshold; int32_t _IF_freq;
  uint32_t _xtal_freq; uint16_t _bandwidth; uint8_t _band_mode,pad,_lever_mode,_brightness; uint32_t checksum;
} config_t;
extern properties_t current_props;
extern config_t config;
extern uint16_t lastsaveid;
extern uint8_t sweep_mode;
extern bool calibration_in_progress;
extern float measured[2][401][2];
#define frequency0 current_props._frequency0
#define frequency1 current_props._frequency1
#define cal_frequency0 current_props._cal_frequency0
#define cal_frequency1 current_props._cal_frequency1
#define sweep_points current_props._sweep_points
#define cal_sweep_points current_props._cal_sweep_points
#define cal_status current_props._cal_status
#define cal_power current_props._cal_power
#define cal_data current_props._cal_data
#define cal_load_r current_props._cal_load_r
static inline uint32_t __ROR(uint32_t v,unsigned n) { return (v>>n)|(v<<(32-n)); }
void chThdYield(void);
void chSemObjectInit(semaphore_t *,int);
msg_t chSemWaitTimeout(semaphore_t *,unsigned);
void chSemSignal(semaphore_t *);
void flash_program_half_word_buffer(uint16_t *,uint16_t *,uint16_t);
void flash_erase_pages(uint32_t,unsigned);
int config_save(void);
int caldata_save(uint32_t);
int caldata_recall(uint32_t);
const properties_t *get_properties(uint32_t);
void clear_all_config_prop_data(void);
void cal_done(void);
bool cal_collect(uint16_t);
void request_to_redraw(uint16_t);
bool app_measurement_sweep(bool,uint16_t);
void app_measurement_update_frequencies(void);
bool need_interpolate(freq_t,freq_t,uint16_t);
uint32_t si5351_get_harmonic_lvl(uint32_t);
void ui_message_box_draw(const char *,const char *);
typedef int event_bus_t;
typedef struct { int topic; } event_bus_message_t;
#define EVENT_CONFIGURATION_CHANGED 1
#define EVENT_STORAGE_UPDATED 2
void event_bus_publish(event_bus_t *,int,void *);
void event_bus_subscribe(event_bus_t *,int,void (*)(const event_bus_message_t *,void *),void *);
'''
HARNESS = r'''
#define _GNU_SOURCE
#include "nanovna.h"
#include "processing/antenna_cal.h"
#include "sys/config_service.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
properties_t current_props;
config_t config={._harmonic_freq_threshold=300000000,._IF_freq=12000,._xtal_freq=26000000,._band_mode=3};
uint8_t sweep_mode=1;
bool calibration_in_progress;
float measured[2][401][2];
static unsigned capture[3], writes, bad_write, fail_capture;
static uint8_t saved[32768];
void chThdYield(void) {}
void chSemObjectInit(semaphore_t *s,int n) { *s=n; }
msg_t chSemWaitTimeout(semaphore_t *s,unsigned t) { (void)t; assert(*s==1); *s=0; return MSG_OK; }
void chSemSignal(semaphore_t *s) { assert(*s==0); *s=1; }
void event_bus_publish(event_bus_t *b,int t,void *d) { (void)b;(void)t;(void)d; }
void event_bus_subscribe(event_bus_t *b,int t,void (*f)(const event_bus_message_t *,void *),void *d) {
  (void)b;(void)t;(void)f;(void)d;
}
void flash_program_half_word_buffer(uint16_t *dst,uint16_t *data,uint16_t size) {
  uintptr_t a=(uintptr_t)dst;
  assert(a>=SAVE_PROP_CONFIG_ADDR && a+size<=0x08040000u);
  memset(dst,255,(size+2047u)&~2047u); memcpy(dst,data,size); writes++;
  if (bad_write) { ((uint8_t *)dst)[size-1]^=1; bad_write=0; }
}
void flash_erase_pages(uint32_t addr,unsigned size) { memset((void *)(uintptr_t)addr,255,size); }
void request_to_redraw(uint16_t m) { (void)m; }
void app_measurement_update_frequencies(void) {}
bool need_interpolate(freq_t a,freq_t b,uint16_t n) {
  return a!=cal_frequency0 || b!=cal_frequency1 || n!=cal_sweep_points;
}
static uint32_t harmonic_split=300000000;
uint32_t si5351_get_harmonic_lvl(uint32_t f) { return f>=harmonic_split; }
void ui_message_box_draw(const char *a,const char *b) { (void)a;(void)b; }
bool app_measurement_sweep(bool breaking,uint16_t mask) {
  assert(!breaking && mask==SWEEP_CH0_MEASURE);
  if (fail_capture) { fail_capture=0; return false; }
  unsigned stage=antenna_cal_stage(), p=antenna_cal_capture-1;
  assert(stage>=1 && stage<=3 && p<2 && sweep_points==401);
  capture[stage-1] |= 1u<<p;
  uint32_t prev=0;
  for (unsigned i=0;i<401;++i) {
    uint32_t f=antenna_cal_frequency(p,i); assert(f>prev); prev=f;
    float gamma=stage==1 ? 1 : stage==2 ? -1 : 0;
    measured[0][i][0]=0.02f+0.8f*gamma/(1-0.1f*gamma);
    measured[0][i][1]=0.01f;
  }
  return true;
}
static void close_enough(float a,float b) { assert(fabsf(a-b)<1e-5f); }
static void finish_capture(void) {
  for(unsigned i=0;i<3;++i) assert(antenna_cal_step());
  assert(antenna_cal_stage()==4);
}
int main(void) {
#ifdef _WIN32
  assert(VirtualAlloc((void *)0x08000000,0x40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x08000000);
#else
  assert(mmap((void *)0x08000000,0x40000,PROT_READ|PROT_WRITE,
              MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)0x08000000);
#endif
  memset((void *)0x08000000,255,0x40000);
  config_service_init();
  frequency0=14000000; frequency1=14350000; sweep_points=101;
  current_props._power=3;
  current_props._portz=cal_load_r=50;
  cal_frequency0=frequency0; cal_frequency1=frequency1; cal_sweep_points=sweep_points;
  for(unsigned i=0;i<TRACES_MAX;++i) current_props._trace[i].scale=1;
  // HF retains the published mapping; V/U adds EU868 and redistributes points.
  // An FNV-style word hash guards the exact persisted frequency/index mapping.
  const uint32_t grid_fingerprint[]={0x960fe1d7u,0xd68238fbu};
  for(unsigned p=0;p<2;++p) {
    uint32_t prev=0, fingerprint=2166136261u;
    for(unsigned i=0;i<401;++i) {
      uint32_t f=antenna_cal_frequency(p,i); assert(f>prev); prev=f;
      fingerprint=(fingerprint^f)*16777619u;
      unsigned j; float k;
      assert(antenna_cal_bracket(p,f,&j,&k));
      assert((j==i && k==0) || (j+1==i && k==1));
    }
    assert(fingerprint==grid_fingerprint[p]);
  }
  // HF coverage includes every endpoint with no interval above 10 kHz.
  const uint32_t hf[][2]={{1800000,2000000},{3500000,4000000},{5330600,5406400},
    {7000000,7300000},{10100000,10150000},{14000000,14350000},{18068000,18168000},
    {21000000,21450000},{24890000,24990000},{28000000,29700000}};
  unsigned point=0;
  for(unsigned band=0;band<10;band++) {
    uint32_t previous=antenna_cal_frequency(0,point++);
    assert(previous==hf[band][0]);
    while(previous<hf[band][1]) {
      assert(point<ANT_CAL_POINTS);
      uint32_t f=antenna_cal_frequency(0,point++);
      assert(f>previous && f-previous<=10000 && f<=hf[band][1]);
      previous=f;
    }
  }
  assert(point==ANT_CAL_POINTS);
  // All V/U endpoints, proportionate spacing and gaps, in increasing order.
  const uint32_t vu[][3]={{50000000,54000000,13},{144000000,148000000,13},
    {222000000,225000000,10},{420000000,450000000,89},{863000000,870000000,22},
    {902000000,928000000,77},{1240000000,1300000000,177}};
  point=0;
  for(unsigned b=0;b<7;b++) {
    uint32_t prev=0;
    assert(antenna_cal_range(1,vu[b][0],vu[b][1]));
    for(unsigned n=0;n<vu[b][2];n++) {
      uint32_t f=antenna_cal_frequency(1,point++);
      assert(f>=vu[b][0] && f<=vu[b][1]);
      if(n==0) assert(f==vu[b][0]);
      else assert(f>prev && f-prev<=342106);
      if(n+1==vu[b][2]) assert(f==vu[b][1]);
      prev=f;
    }
    unsigned index; float fraction;
    assert(!antenna_cal_bracket(1,vu[b][0]-1,&index,&fraction));
    assert(!antenna_cal_bracket(1,vu[b][1]+1,&index,&fraction));
  }
  assert(point==ANT_CAL_POINTS);
  assert(!antenna_cal_range(1,870000000,902000000));
  unsigned j; float k;
  assert(!antenna_cal_bracket(0,5000000,&j,&k));
  assert(!antenna_cal_range(1,148000000,222000000));
  assert(!antenna_cal_frequency(2,0) && !antenna_cal_frequency(0,401));
  assert(caldata_save(4)==0);
  unsigned before=writes;
  assert(caldata_save(5)==-1 && caldata_save(6)==-1);
  assert(config_service_api()->save_calibration(5)==-1);
  assert(antenna_cal_store(0)==-1 && writes==before);
  memcpy(saved,(void *)0x08030800,32768);
  assert(antenna_cal_begin(1)); // Missing V/U is forcibly included.
  fail_capture=1; assert(!antenna_cal_step() && antenna_cal_stage()==1);
  bad_write=1; assert(!antenna_cal_step() && antenna_cal_stage()==1);
  assert(antenna_cal_step());
  antenna_cal_cancel();
  assert(!memcmp(saved,(void *)0x08030800,32768));
  assert(frequency0==14000000 && frequency1==14350000 && sweep_points==101 && sweep_mode==1);
  assert(antenna_cal_begin(1)); finish_capture();
  assert(capture[0]==3 && capture[1]==3 && capture[2]==3);
  assert(antenna_cal_step() && !antenna_cal_stage());
  assert(antenna_cal_saved(0) && antenna_cal_saved(1));
  for(unsigned p=0;p<2;++p) {
    cal_load_r=75;
    assert(antenna_cal_load(p));
    assert(cal_load_r==50);
    for(unsigned i=0;i<401;++i) {
      close_enough(cal_data[0][i][0],0.02f);
      close_enough(cal_data[1][i][0],0.1f);
      close_enough(cal_data[2][i][0],0.8f);
    }
    float out[5][2];
    assert(antenna_cal_interpolate(antenna_cal_frequency(p,19),out));
    close_enough(out[1][0],0.1f); close_enough(out[2][0],0.8f);
    assert(antenna_cal_interpolate(30000000,out));
    close_enough(out[0][0],0); close_enough(out[2][0],1); // gap: identity
  }
  memcpy(saved,(void *)0x08030800,32768);
  memset(capture,0,sizeof capture);
  // A nonconstant table verifies fractional interpolation on the EU868 grid.
  assert(antenna_cal_load(1));
  for(unsigned i=0;i<401;i++) cal_data[0][i][0]=(float)i;
  float interpolated[5][2]; unsigned bracket; float fraction;
  assert(antenna_cal_bracket(1,868100000,&bracket,&fraction));
  assert(fraction>0 && fraction<1);
  assert(antenna_cal_interpolate(868100000,interpolated));
  close_enough(interpolated[0][0],bracket+fraction);
  // Never blend coefficients across a clock harmonic transition, even when
  // it lies inside a segment rather than in an uncalibrated band gap.
  harmonic_split=868100000;
  assert(antenna_cal_interpolate(harmonic_split-1,interpolated));
  close_enough(interpolated[0][0],bracket);
  assert(antenna_cal_interpolate(harmonic_split,interpolated));
  close_enough(interpolated[0][0],bracket+1);
  harmonic_split=300000000;
  assert(antenna_cal_begin(1)); finish_capture();
  assert(capture[0]==1 && capture[1]==1 && capture[2]==1); // Keep existing V/U.
  assert(antenna_cal_step());
  assert(!memcmp(saved+16384,(void *)0x08034800,16384));
  // Upgrade path: recapture only V/U and preserve the complete HF flash slot.
  memcpy(saved,(void *)0x08030800,32768); memset(capture,0,sizeof capture);
  assert(antenna_cal_begin(2)); finish_capture();
  assert(capture[0]==2 && capture[1]==2 && capture[2]==2);
  assert(antenna_cal_step());
  assert(!memcmp(saved,(void *)0x08030800,16384));
  memcpy(saved,(void *)0x08030800,32768);
  *(uint8_t *)(uintptr_t)(SAVE_PROP_CONFIG_ADDR-1)=0x5a;
  *(uint8_t *)(uintptr_t)(SAVE_PROP_CONFIG_ADDR+SAVE_FULL_AREA_SIZE)=0xa5;
  clear_all_config_prop_data();
  for(unsigned i=0;i<SAVE_FULL_AREA_SIZE;i++)
    assert(((const uint8_t *)(uintptr_t)SAVE_PROP_CONFIG_ADDR)[i]==255);
  assert(*(uint8_t *)(uintptr_t)(SAVE_PROP_CONFIG_ADDR-1)==0x5a);
  assert(*(uint8_t *)(uintptr_t)(SAVE_PROP_CONFIG_ADDR+SAVE_FULL_AREA_SIZE)==0xa5);
  for(unsigned slot=0;slot<SAVEAREA_MAX;slot++) assert(!get_properties(slot));
  assert(!antenna_cal_saved(0) && !antenna_cal_saved(1));
  assert(!antenna_cal_load(0) && !antenna_cal_load(1));
  memset(capture,0,sizeof capture);
  assert(antenna_cal_begin(1)); finish_capture();
  assert(capture[0]==3 && capture[1]==3 && capture[2]==3);
  assert(antenna_cal_step());
  assert(antenna_cal_saved(0) && antenna_cal_saved(1));
  assert(antenna_cal_load(0));
  assert(antenna_cal_applied(0) && !antenna_cal_applied(1));
  current_props._power ^= 1;
  assert(!antenna_cal_applied(0)); current_props._power ^= 1;
  config._IF_freq++;
  assert(!antenna_cal_applied(0)); config._IF_freq--;
  config._harmonic_freq_threshold++;
  assert(!antenna_cal_applied(0)); config._harmonic_freq_threshold--;
  cal_status &= ~CALSTAT_APPLY;
  assert(!antenna_cal_applied(0)); cal_status |= CALSTAT_APPLY;
  assert(antenna_cal_applied(0));
  config._band_mode=0;
  assert(!antenna_cal_saved(0) && !antenna_cal_load(0) && !(cal_status&CALSTAT_APPLY));
  config._band_mode=3;
  assert(antenna_cal_begin(3)); finish_capture();
  bad_write=1; assert(!antenna_cal_step()); // Failed final write cannot report success.
  assert(!get_properties(5)); // Cache invalidated; corrupt checksum rejected.
  antenna_cal_cancel(); assert(!(cal_status&CALSTAT_APPLY));
  puts("ANT: grids, OSL coefficients, dual capture, empty-slot inclusion, keep/cancel, write guards and failures passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    tmp = Path(d)
    (tmp/'nanovna.h').write_text(HEADER)
    for name in ('ch.h','hal.h','rf/sweep.h','driver/si5351.h','ui/core/ui_core.h','sys/event_bus.h'):
        p=tmp/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_text('#include "nanovna.h"\n')
    (tmp/'test.c').write_text(HARNESS)
    sources=['src/processing/antenna.c','src/processing/antenna_cal_grid.c','src/processing/antenna_cal.c',
             'src/processing/calibration.c','src/sys/config_service.c']
    command=shlex.split(os.environ.get('HOST_CC','cc'))
    subprocess.run(command+['-std=c11','-Wall','-Wextra','-Werror','-Wno-int-to-pointer-cast',
        '-DNANOVNA_F303','-I'+str(tmp),'-I'+str(ROOT/'include'),str(tmp/'test.c')]+
        [str(ROOT/s) for s in sources]+['-lm','-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True,timeout=20)
