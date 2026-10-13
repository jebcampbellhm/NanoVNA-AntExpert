"""Exercise production numeric entry, including H4 keypad modes above bit 31."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/ui/core/ui_keypad.c').read_text(encoding='utf-8')

def section(start, end):
    return source[source.index(start):source.index(end, source.index(start))]

CORE = r'''
#pragma once
#include <stdint.h>
#define NANOVNA_F303
#define __USE_RTC__
#define __USE_SD_CARD__
#define __SD_CARD_DUMP_TIFF__
#define __SD_CARD_DUMP_FIRMWARE__
#define __S11_CABLE_MEASURE__
#define __S21_MEASURE__
typedef uint32_t freq_t;
typedef struct { unsigned unused; } button_t;
typedef void (*keyboard_cb_t)(uint16_t, button_t *);
#define UI_KEYBOARD_CALLBACK(name) void name(uint16_t data, button_t *b)
'''
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui/core/ui_keypad.h"
#define LCD_HEIGHT 320
#define NUM_INPUT_HEIGHT 40
#define NUM_FONT_GET_WIDTH 16
#define NUM_FONT_GET_HEIGHT 22
#define LCD_INPUT_TEXT_COLOR 1
#define LCD_INPUT_BG_COLOR 0
#define FONT_STR_WIDTH(n) (7*(n))
char kp_buf[NUMINPUT_LEN+2];
uint8_t keypad_mode;
static void lcd_set_colors(int a,int b) { (void)a;(void)b; }
static void lcd_fill(int x,int y,int w,int h) { (void)x;(void)y;(void)w;(void)h; }
static void lcd_drawfont(int c,int x,int y) { assert(c>=0 && c<=12); (void)x;(void)y; }
PRODUCTION_CODE
static void type(const char *s) {
  kp_buf[0]=0;
  for(;*s;++s) assert(num_keypad_click(*s=='.'?KP_PERIOD:*s-'0',strlen(kp_buf))==K_CONTINUE);
}
static void frequency(const char *s,int unit,unsigned long expected) {
  type(s); assert(num_keypad_click(unit,strlen(kp_buf))==K_DONE);
  assert(strtoul(kp_buf,NULL,10)==expected);
}
int main(void) {
  // A bit-mask comparison here used to shift a 32-bit int by >=32.
  _Static_assert(KM_ANT_CABLE_FREQ>=32,"exercise the high keypad index");
  keypad_mode=KM_ANT_CABLE_FREQ;
  frequency("14.2",KP_M,14200000);
  frequency("0.05",KP_M,50000);
  frequency("7100",KP_k,7100000);
  frequency("2.7",KP_G,2700000000UL);
  type("1234567890123"); assert(!strcmp(kp_buf,"123456789012"));
  type("7.1.2"); assert(!strcmp(kp_buf,"7.12"));
  kp_buf[0]=0;
  assert(num_keypad_click(KP_X1,0)==K_CANCEL);
  assert(num_keypad_click(KP_BS,0)==K_CANCEL);
  type("7"); assert(num_keypad_click(KP_BS,1)==K_CONTINUE && !kp_buf[0]);
  keypad_mode=KM_RTC_DATE; type("2610137"); assert(!strcmp(kp_buf,"261013"));
  keypad_mode=KM_RTC_TIME; type("1234567"); assert(!strcmp(kp_buf,"123456"));
  keypad_mode=KM_START; frequency("14.2",KP_M,14200000);
  puts("numeric keypad: MHz/kHz/GHz, high mode index, cancel, limits and RTC entry passed");
}
'''
production = '\n'.join([
    section('#define KP_EMPTY', 'typedef struct'),
    section('static int period_pos(', 'static void draw_numeric_area_frame('),
    section('static void draw_numeric_input(', 'static void draw_text_input('),
    section('enum { K_CONTINUE', 'static int txt_keypad_click('),
])
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d / 'ui/core').mkdir(parents=True)
    (d / 'ui/core/ui_core.h').write_text(CORE, encoding='utf-8')
    (d / 'test.c').write_text(HARNESS.replace('PRODUCTION_CODE', production), encoding='utf-8')
    subprocess.run(shlex.split(os.environ.get('HOST_CC', 'cc')) + [
        '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(d), '-I'+str(ROOT/'include'),
        str(d/'test.c'), '-o', str(d/'test')], check=True)
    subprocess.run([str(d/'test')], check=True)
