"""Exercise the production antenna UI with host LCD/RF/storage ports.

Set ANTENNA_PREVIEW_DIR to save 480x320 PPM previews using the actual H4 font.
No RF hardware or calibration accuracy is simulated by this test.
"""
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
#include <stdio.h>
#define NANOVNA_F303 1
#define SWEEP_POINTS_MAX 401
#define FONT_START_CHAR 0x16
#define SWEEP_ENABLE 1
#define DOMAIN_MODE 1
#define TD_CENTER_SPAN 64
#define CALSTAT_ED 1
#define CALSTAT_ES 32
#define CALSTAT_ER 64
#define CALSTAT_APPLY 256
#define CALSTAT_INTERPOLATED 512
#define REDRAW_ALL 65535
#define LCD_FG_COLOR 1
#define LCD_BG_COLOR 0
#define LCD_TRACE_1_COLOR 2
#define UI_NORMAL 0
#define UI_MENU 1
#define EVT_BUTTON_SINGLE_CLICK 1
#define EVT_UP 2
#define EVT_DOWN 4
#define LCD_WIDTH 480
#define LCD_HEIGHT 320
#define SPI_BUFFER_SIZE 4096
typedef uint16_t pixel_t;
extern pixel_t spi_buffer[SPI_BUFFER_SIZE];
extern const uint8_t x7x11b_bits[];
#define FONT_GET_HEIGHT 11
#define FONT_GET_DATA(ch) (x7x11b_bits+((ch)-FONT_START_CHAR)*11)
#define FONT_GET_WIDTH(ch) (8-(FONT_GET_DATA(ch)[0]&7))
#define GET_PALTETTE_COLOR(i) (i)
void lcd_bulk_finish(void);
void lcd_bulk(int,int,int,int);
typedef uint32_t freq_t;
typedef int menuitem_t;
extern const menuitem_t menu_cal_menu[];
extern uint32_t frequency0, frequency1, cal_frequency0, cal_frequency1;
extern uint16_t sweep_points, props_mode, cal_status;
extern uint8_t sweep_mode, ui_mode;
extern struct config_stub { uint8_t _reserved[3]; } config;
extern struct props_stub { float _portz; } current_props;
extern float measured[2][401][2];
void request_to_redraw(uint16_t mask);
void lcd_drawstring_size(const char *,int,int,uint8_t);
void lcd_line(int,int,int,int);
void lcd_set_colors(int,int);
void lcd_set_foreground(int);
void lcd_clear_screen(void);
#define plot_printf snprintf
void app_measurement_update_frequencies(void);
void ui_mode_normal(void);
void ui_mode_menu(void);
void menu_push_submenu(const menuitem_t *);
void touch_wait_release(void);
int config_save(void);
void ui_message_box(const char *,const char *,uint32_t);
freq_t get_frequency(unsigned i);
'''
HARNESS = r'''
#include "nanovna.h"
#include "ui/antenna_ui.h"
#include "processing/antenna.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
uint32_t frequency0=1000000, frequency1=30000000, cal_frequency0, cal_frequency1;
uint16_t sweep_points=101, props_mode=DOMAIN_MODE|TD_CENTER_SPAN, cal_status;
uint8_t sweep_mode=0, ui_mode=UI_MENU;
struct config_stub config;
struct props_stub current_props={75};
float measured[2][401][2];
const menuitem_t menu_cal_menu[]={1};
static unsigned writes, messages, updates, menus;
static bool fail_save;
static int color=1;
static uint8_t pixels[320][480];
pixel_t spi_buffer[SPI_BUFFER_SIZE];
static unsigned strip_transfers, next_strip;
static bool buffer_ready;
void lcd_bulk_finish(void) { buffer_ready=true; }
void lcd_bulk(int x,int y,int w,int h) {
  assert(buffer_ready); // Rendering must finish prior DMA before using SPI RAM.
  assert(x==0 && y==(int)next_strip && w==480 && h==8);
  for(int row=0;row<h;row++) for(int col=0;col<w;col++)
    pixels[y+row][col]=spi_buffer[row*w+col];
  next_strip=(next_strip+h)%320;
  strip_transfers++;
}
extern const uint8_t x7x11b_bits[];
void request_to_redraw(uint16_t m) { (void)m; }
void lcd_set_colors(int f,int b) { assert(b==0); color=f; }
void lcd_set_foreground(int f) { color=f; }
void lcd_clear_screen(void) { assert(!"antenna redraw must never blank the LCD"); }
static void pixel(int x,int y,int c) {
  assert(x>=0 && x<480 && y>=0 && y<320);
  pixels[y][x]=c;
}
void lcd_drawstring_size(const char *s,int x,int y,uint8_t scale) {
  while (*s) {
    unsigned ch=(unsigned char)*s++;
    assert(ch>=0x16 && ch<128);
    const uint8_t *glyph=x7x11b_bits+(ch-0x16)*11;
    int w=8-(glyph[0]&7);
    assert(w*11*scale*scale<=4096); // Firmware SPI buffer capacity.
    for (int row=0;row<11;++row)
      for (int col=0;col<w;++col)
        for (int a=0;a<scale;++a)
          for (int b=0;b<scale;++b)
            pixel(x+col*scale+b,y+row*scale+a,(glyph[row]&(0x80>>col))?color:0);
    x+=w*scale;
  }
}
void lcd_line(int x,int y,int x1,int y1) {
  int dx=abs(x1-x),sx=x<x1?1:-1,dy=-abs(y1-y),sy=y<y1?1:-1,err=dx+dy;
  for (;;) {
    pixel(x,y,color); if(x==x1 && y==y1) break;
    int e=2*err;
    if(e>=dy) { err+=dy; x+=sx; }
    if(e<=dx) { err+=dx; y+=sy; }
  }
}
void app_measurement_update_frequencies(void) { updates++; }
void ui_mode_normal(void) { ui_mode=UI_NORMAL; }
void ui_mode_menu(void) { ui_mode=UI_MENU; }
void menu_push_submenu(const menuitem_t *m) { assert(m==menu_cal_menu); menus++; }
void touch_wait_release(void) {}
int config_save(void) { writes++; return fail_save?-1:0; }
void ui_message_box(const char *a,const char *b,uint32_t t) { (void)a;(void)b;(void)t;messages++; }
freq_t get_frequency(unsigned i) {
  assert(i<sweep_points);
  return frequency0+(uint64_t)(frequency1-frequency0)*i/(sweep_points-1);
}
static void preview(const char *name) {
  assert(antenna_ui_draw());
  const char *dir=getenv("ANTENNA_PREVIEW_DIR"); if(!dir) return;
  char path[1024]; snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);
  FILE *f=fopen(path,"wb"); assert(f); fprintf(f,"P6\n480 320\n255\n");
  for(unsigned y=0;y<320;y++) for(unsigned x=0;x<480;x++) {
    const uint8_t rgb[3]={pixels[y][x]?240:10,pixels[y][x]?240:14,pixels[y][x]==1?240:20};
    fwrite(rgb,1,3,f);
  }
  fclose(f);
}
static void sample(void) {
  for(unsigned i=0;i<sweep_points;i++) {
    float d=(float)i/(sweep_points-1)-0.44f;
    measured[0][i][0]=0.1f+2*d*d; measured[0][i][1]=0;
  }
  antenna_ui_measurement();
}
int main(void) {
  assert(!antenna_ui_draw()); assert(!antenna_ui_touch(10,10));
  antenna_ui_enter(0);
  assert(frequency0==14000000 && frequency1==14350000 && sweep_points==101);
  assert(current_props._portz==50 && !(props_mode&DOMAIN_MODE) && sweep_mode==1);
  preview("waiting"); sample(); preview("sweep");
  // A repeated frame must replace complete strips, without clearing the LCD,
  // changing pixels, or using a second framebuffer in firmware.
  uint8_t previous[320][480]; memcpy(previous,pixels,sizeof(pixels));
  unsigned transfers_before=strip_transfers;
  buffer_ready=false;
  assert(antenna_ui_draw());
  assert(strip_transfers==transfers_before+40 && next_strip==0);
  assert(memcmp(previous,pixels,sizeof(pixels))==0);
  uint32_t before=antenna_favorites_decode(config._reserved);
  antenna_ui_touch(280,10); assert(writes==1);
  assert(antenna_favorites_decode(config._reserved)==(before^(1u<<5)));
  fail_save=true; antenna_ui_touch(280,10); assert(messages==1);
  assert(antenna_favorites_decode(config._reserved)==(before^(1u<<5)));
  fail_save=false;
  antenna_ui_touch(260,295);
  assert(frequency0==frequency1 && sweep_points==21);
  uint32_t cw=frequency0; sample(); preview("tune");
  antenna_ui_touch(400,200); assert(frequency0>cw && frequency0==frequency1);
  antenna_ui_lever(EVT_DOWN); assert(frequency0==cw);
  for(unsigned i=0;i<300;i++) antenna_ui_lever(EVT_UP);
  assert(frequency0==14350000);
  for(unsigned i=0;i<300;i++) antenna_ui_lever(EVT_DOWN);
  assert(frequency0==14000000);
  antenna_ui_touch(400,295); assert(menus==1 && ui_mode==UI_MENU);
  assert(frequency0==14000000 && frequency1==14350000 && sweep_points==101);
  assert(!antenna_ui_draw()); ui_mode_normal();
  cal_frequency0=14000000; cal_frequency1=14350000;
  cal_status=CALSTAT_APPLY|CALSTAT_ED|CALSTAT_ES|CALSTAT_ER;
  sample(); preview("calibrated");
  cal_frequency1=14200000; preview("outside-cal");
  frequency1=15000000; preview("range-changed");
  antenna_ui_touch(180,295); assert(frequency1==14350000);
  antenna_ui_touch(20,295); preview("bands-hf");
  antenna_ui_touch(200,295); preview("bands-hf-page2");
  antenna_ui_touch(200,50); preview("bands-vhf");
  antenna_ui_touch(20,95); // 2 m
  assert(frequency0==144000000 && frequency1==148000000);
  sample(); preview("2m");
  antenna_ui_touch(20,295); antenna_ui_touch(340,50); preview("favorites");
  antenna_ui_touch(400,295); antenna_ui_touch(400,10);
  assert(!antenna_ui_draw());
  assert(frequency0==1000000 && frequency1==30000000 && sweep_points==101);
  assert(props_mode==(DOMAIN_MODE|TD_CENTER_SPAN) && current_props._portz==75 && sweep_mode==0);
  assert(updates>600);
  // One 60 m tile covers all five channels; guides are drawn at their centers.
  antenna_ui_enter(0); antenna_ui_touch(20,295); antenna_ui_touch(20,50);
  antenna_ui_touch(20,150);
  assert(frequency0==5330600 && frequency1==5406400);
  preview("60m-channels");
  for(unsigned i=0;i<ANTENNA_60M_CHANNELS;i++) {
    unsigned x=42+(uint64_t)(antenna_60m_centers[i]-frequency0)*424/(frequency1-frequency0);
    assert(pixels[190][x]==1 && pixels[191][x]==1 && pixels[192][x]==0);
  }
  sample(); preview("60m-sweep");
  antenna_ui_touch(400,10);
  // Every band at maximum point count, graph ceiling, and large readout.
  sweep_points=401; antenna_ui_enter(0);
  for(unsigned g=0;g<2;g++) for(unsigned p=0;p<(g?1:2);p++)
    for(unsigned j=0;j<6;j++) {
      if(g && j==5) break;
      antenna_ui_touch(20,295); antenna_ui_touch(g*160+20,50);
      for(unsigned k=0;k<p;k++) antenna_ui_touch(180,295);
      antenna_ui_touch((j%2)*240+20,90+(j/2)*58);
      if(!g && p==1 && j>=5) { antenna_ui_touch(400,295); continue; }
      for(unsigned i=0;i<sweep_points;i++) measured[0][i][0]=1;
      antenna_ui_measurement(); assert(antenna_ui_draw());
    }
  puts("antenna UI transitions, persistence, bounds and rendering passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    tmp = Path(d)
    (tmp / 'nanovna.h').write_text(HEADER)
    for name in ('ui/core/ui_core.h', 'ui/core/ui_menu_engine.h', 'ui/menus/menu_cal.h'):
        p = tmp / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#include "nanovna.h"\n')
    (tmp / 'harness.c').write_text(HARNESS)
    command = shlex.split(os.environ.get('HOST_CC', 'cc'))
    subprocess.run(command + ['-std=c11', '-Wall', '-Wextra', '-Werror', '-DNANOVNA_F303=1',
        '-I' + str(tmp), '-I' + str(ROOT / 'include'), str(tmp / 'harness.c'),
        str(ROOT / 'src/ui/antenna_ui.c'), str(ROOT / 'src/processing/antenna.c'),
        str(ROOT / 'src/ui/resources/fonts/Font7x11b.c'), '-lm', '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
