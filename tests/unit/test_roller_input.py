"""Run production roller input and menu navigation against clock/GPIO stubs."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = r'''
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t systime_t;
#define MS2ST(x) (x)
#define GPIOA_LEVER1 1
#define GPIOA_PUSH 2
#define GPIOA_LEVER2 3
#define GPIOA 0
#define WDGD1 0
uint16_t palReadPort(int);
systime_t chVTGetSystemTimeX(void);
void chThdSleepMilliseconds(unsigned);
void wdgReset(const int *);
'''
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include "ui/core/hardware_input.h"
static uint32_t now, release_at;
static uint16_t pins;
uint16_t palReadPort(int p) { (void)p; return pins; }
systime_t chVTGetSystemTimeX(void) { return now; }
void chThdSleepMilliseconds(unsigned ms) {
  now += ms;
  if (release_at && now >= release_at) { pins=0; release_at=0; }
}
void wdgReset(const int *p) { (void)p; }
static void reset(void) { pins=0; now=100; release_at=0; ui_input_reset_state(); now+=30; }
static int8_t selection;
static int closed, invoked, draws;
uint16_t current_menu_get_count(void) { return 5; }
void ui_mode_normal(void) { closed++; }
void menu_invoke(int i) { invoked=i+1; }
void menu_draw(uint32_t mask) { assert(mask && !(mask & ~31U)); draws++; }
MENU_FUNCTION
static void turn(uint16_t button, uint16_t event) {
  now+=30; pins=button;
  assert(ui_input_check()==event);
  release_at=now+40;
  ui_menu_lever(event);
}
int main(void) {
  reset(); pins=BUTTON_PUSH;
  assert(ui_input_check()==EVT_BUTTON_SINGLE_CLICK);
  pins=0; now+=30; assert(ui_input_check()==0);
  pins=BUTTON_PUSH; now+=30; assert(ui_input_check()==EVT_BUTTON_SINGLE_CLICK);
  release_at=now+20; assert(ui_input_wait_release()==0);
  now+=30; pins=BUTTON_PUSH; assert(ui_input_check()==EVT_BUTTON_SINGLE_CLICK);
  pins=0; assert(ui_input_wait_release()==0);
  assert(ui_input_wait_release()==0); // idle must return, not wait 500 ms
  reset(); pins=BUTTON_UP; assert(ui_input_check()==EVT_UP);
  assert(ui_input_wait_release()==(EVT_UP|EVT_REPEAT));
  uint32_t first=now;
  assert(ui_input_wait_release()==(EVT_UP|EVT_REPEAT));
  assert(now-first>=BUTTON_REPEAT_TICKS);
  release_at=now+10; assert(ui_input_wait_release()==0);
  reset(); now=UINT32_MAX-250; ui_input_reset_state(); now+=30;
  pins=BUTTON_DOWN; assert(ui_input_check()==EVT_DOWN);
  assert(ui_input_wait_release()==(EVT_DOWN|EVT_REPEAT)); // tick wrap
  pins=0; assert(ui_input_wait_release()==0);
  reset(); selection=-1; turn(BUTTON_UP,EVT_UP);
  assert(selection==0 && !closed);
  turn(BUTTON_UP,EVT_UP); assert(selection==1 && !closed);
  turn(BUTTON_DOWN,EVT_DOWN); assert(selection==0 && !closed);
  selection=-1; turn(BUTTON_DOWN,EVT_DOWN); assert(selection==4 && !closed);
  ui_menu_lever(EVT_BUTTON_SINGLE_CLICK); assert(invoked==5);
  turn(BUTTON_UP,EVT_UP); assert(closed==1);
  assert(draws==4);
  puts("roller release, repeated presses, hold repeat, tick wrap and menu entry passed");
}
'''
source = (ROOT / 'src/ui/core/ui_menu_engine.c').read_text()
menu = source[source.index('void ui_menu_lever('):source.index('void ui_menu_touch(')]
core = (ROOT / 'src/ui/core/ui_core.c').read_text()
assert core.count('EXT_CH_MODE_BOTH_EDGES') == 6
with tempfile.TemporaryDirectory() as d:
    tmp = Path(d)
    for name in ('hal.h', 'ch.h', 'nanovna.h'):
        (tmp / name).write_text(HEADER.replace('#define WDGD1 0', 'extern const int WDGD1;'))
    (tmp / 'test.c').write_text(HARNESS.replace('MENU_FUNCTION', menu) + '\nconst int WDGD1=0;\n')
    command = shlex.split(os.environ.get('HOST_CC', 'cc'))
    subprocess.run(command + ['-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I'+str(tmp), '-I'+str(ROOT/'include'), str(tmp/'test.c'),
        str(ROOT/'src/ui/core/hardware_input.c'), '-o', str(tmp/'test')], check=True)
    subprocess.run([str(tmp/'test')], check=True, timeout=10)
