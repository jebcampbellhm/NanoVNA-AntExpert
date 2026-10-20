"""Exercise the production formatter used by both VNA and easy pages."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HAL = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
static inline float infinityf(void) { return INFINITY; } // newlib helper
typedef int msg_t;
#define MSG_OK 0
#define _base_sequential_stream_methods \
  size_t (*write)(void *, const uint8_t *, size_t); \
  size_t (*read)(void *, uint8_t *, size_t); \
  msg_t (*put)(void *, uint8_t); msg_t (*get)(void *);
struct StreamVMT { _base_sequential_stream_methods };
typedef struct { const struct StreamVMT *vmt; } BaseSequentialStream;
#define streamPut(s,c) ((s)->vmt->put((s),(c)))
'''
HARNESS = r'''
#include "hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int plot_printf(char *,int,const char *,...);
int main(void) {
  char s[80];
  plot_printf(s,sizeof s,"%.2f m",30.48); assert(!strcmp(s,"30.48 m"));
  plot_printf(s,sizeof s,"%.2f ft",100.0); assert(!strcmp(s,"100.00 ft"));
  plot_printf(s,sizeof s,"VF %u%% / %u MHz",66u,300u); assert(!strcmp(s,"VF 66% / 300 MHz"));
  plot_printf(s,sizeof s,"%.5f MHz",14.175); assert(!strcmp(s,"14.17500 MHz"));
  plot_printf(s,sizeof s,"FREQ %.6f MHz",14.2); assert(!strcmp(s,"FREQ 14.200000 MHz"));
  plot_printf(s,sizeof s,"1/%u: %.2f m / %.2f ft",4u,6.967,22.856);
  assert(!strcmp(s,"1/4: 6.97 m / 22.86 ft"));
  plot_printf(s,sizeof s,"%+.2f / %04u",-1.25,7u); assert(!strcmp(s,"-1.25 / 0007"));
  plot_printf(s,sizeof s,"%.4F",14000000.0); assert(!strcmp(s,"14M"));
  plot_printf(s,sizeof s,"Z %.1f%+j.1f\x1e",50.0,25.0);
  assert(!strcmp(s,"Z 50.0+j25.0\x1e"));
  plot_printf(s,sizeof s,"Z %.1f%+j.1f\x1e",50.0,-25.0);
  assert(!strcmp(s,"Z 50.0-j25.0\x1e"));
  plot_printf(s,sizeof s,"Z %.1f%+j.1f\x1e",0.0,0.0);
  assert(!strcmp(s,"Z 0.0+j0.0\x1e"));
  plot_printf(s,sizeof s,"%q",(uint32_t)14075000); assert(!strcmp(s,"14.075 000 M"));
  char guarded[]="xxxxxxxx";
  assert(plot_printf(guarded+1,4,"abcdef")==3);
  assert(!memcmp(guarded,"xabc\0xxx",8));
  assert(plot_printf(guarded+1,0,"abcdef")==0);
  assert(!memcmp(guarded,"xabc\0xxx",8));
  // A broad set of the ordinary %f/%u/%s formats still matches libc.
  for(unsigned i=0;i<100;++i) {
    char expected[80]; double value=(int)i-50;
    plot_printf(s,sizeof s,"%s %u %.2f","VNA",i,value/4);
    snprintf(expected,sizeof expected,"%s %u %.2f","VNA",i,value/4);
    assert(!strcmp(s,expected));
  }
  puts("VNA/cable numeric formats and bounded output passed");
}
'''
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d / 'hal.h').write_text(HAL)
    (d / 'nanovna.h').write_text('#include "hal.h"\n#define S_MICRO "u"\n#define S_INFINITY "I"\n#define DIGIT_SEPARATOR \'.\'\n')
    (d / 'memstreams.h').write_text('''#include "hal.h"
typedef struct { const struct StreamVMT *vmt; size_t eos; } MemoryStream;
#include <assert.h>
static inline void msObjectInit(MemoryStream *s,uint8_t *b,size_t n,size_t e) {
  (void)s; (void)b; (void)n; (void)e;
  assert(!"plot_printf must not use the OS memory-stream adapter");
}
''')
    (d / 'test.c').write_text(HARNESS)
    subprocess.run(shlex.split(os.environ.get('HOST_CC', 'cc')) + [
        '-std=c11', '-O2', '-ffast-math', '-ffunction-sections', '-fdata-sections',
        '-DNANOVNA_F303', '-DARM_MATH_CM4', '-DCHPRINTF_USE_FLOAT=1',
        '-I'+str(d), '-I'+str(ROOT/'third_party/ChibiOS/os/hal/lib/streams'),
        str(d/'test.c'), str(ROOT/'src/sys/chprintf.c'), '-Wl,--gc-sections',
        '-lm', '-o', str(d/'test')], check=True)
    subprocess.run([str(d/'test')], check=True)
