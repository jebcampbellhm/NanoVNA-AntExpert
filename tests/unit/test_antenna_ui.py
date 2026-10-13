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
runtime = (ROOT / 'src/runtime/runtime_entry.c').read_text()
thread = runtime[runtime.index('static THD_FUNCTION(Thread1, arg)'):]
assert thread.index('ui_port.api->init();') < thread.index('plot_init();') < thread.index('antenna_ui_restore();') < thread.index('while (1)')
HEADER = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define NANOVNA_F303 1
#define SWEEP_POINTS_MAX 401
#define SPEED_OF_LIGHT 299792458.0f
#define FREQUENCY_MAX 2700000000U
extern uint8_t velocity_factor;
extern float electrical_delayS11;
float cable_quarter_wave_frequency(void);
float cable_phase_delay(void);
#define FONT_START_CHAR 0x16
#define SWEEP_ENABLE 1
#define SWEEP_ONCE 2
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
#define LCD_TRACE_5_COLOR 3
#define UI_NORMAL 0
#define UI_MENU 1
#define UI_KEYPAD 2
#define KM_ANT_CABLE_FREQ 33
typedef struct { union { uint32_t u; } p1; } button_t;
#define UI_KEYBOARD_CALLBACK(name) void name(uint16_t data,button_t *b)
UI_KEYBOARD_CALLBACK(input_cable_frequency);
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
#define VNA_MODE_RF_CONFIRMED_MASK 0x8000u
extern struct config_stub { uint8_t _reserved[3]; uint16_t _vna_mode; } config;
typedef struct props_stub { float _portz; uint32_t magic, _reserved1[5]; } properties_t;
extern struct props_stub current_props;
#define CAL_TYPE_COUNT 5
extern float measured[2][401][2];
void request_to_redraw(uint16_t mask);
void lcd_drawstring_size(const char *,int,int,uint8_t);
void lcd_line(int,int,int,int);
void lcd_set_colors(int,int);
void lcd_set_foreground(int);
void lcd_clear_screen(void);
int host_printf(char *,size_t,const char *,...);
#define plot_printf host_printf
void app_measurement_update_frequencies(void);
void ui_mode_normal(void);
void ui_mode_menu(void);
void ui_mode_keypad(int);
freq_t keyboard_get_freq(void);
void menu_push_submenu(const menuitem_t *);
void draw_all(void);
void menu_draw(uint32_t);
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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
uint32_t frequency0=1000000, frequency1=30000000, cal_frequency0, cal_frequency1;
uint16_t sweep_points=101, props_mode=DOMAIN_MODE|TD_CENTER_SPAN, cal_status;
uint8_t sweep_mode=0, ui_mode=UI_MENU, velocity_factor=66;
float electrical_delayS11;
static float cable_test_frequency;
static float cable_test_delay;
float cable_quarter_wave_frequency(void) { return cable_test_frequency; }
float cable_phase_delay(void) { return cable_test_delay; }
struct config_stub config={._vna_mode=VNA_MODE_RF_CONFIRMED_MASK};
struct props_stub current_props={._portz=75};
float measured[2][401][2];
const menuitem_t menu_cal_menu[]={1};
static unsigned writes, messages, updates, menus;
static bool fail_save;
static const char *want_min, *want_max;
static bool saw_min, saw_max;
int host_printf(char *s,size_t size,const char *fmt,...) {
  va_list ap; va_start(ap,fmt); int n=vsnprintf(s,size,fmt,ap); va_end(ap);
  if(want_min && strcmp(s,want_min)==0) saw_min=true;
  if(want_max && strcmp(s,want_max)==0) saw_max=true;
  return n;
}
static uint16_t pending_redraw;
static bool menu_visible;
static unsigned cal_stage, capture_mask;
static bool saved_profiles[2];
bool antenna_cal_saved(unsigned p) { return p<2 && saved_profiles[p]; }
bool antenna_cal_applied(unsigned p) {
  return current_props.magic==0x414e5431 && current_props._reserved1[0]==p &&
         (cal_status&CALSTAT_APPLY);
}
bool antenna_cal_load(unsigned p) {
  assert(p<2);
  if(!saved_profiles[p]) return false;
  current_props.magic=0x414e5431; current_props._reserved1[0]=p;
  cal_frequency0=p?50000000:1800000; cal_frequency1=p?1300000000:29700000;
  cal_status=CALSTAT_APPLY|CALSTAT_ED|CALSTAT_ES|CALSTAT_ER;
  return true;
}
bool antenna_cal_begin(unsigned mask) { capture_mask=mask; cal_stage=1; return true; }
bool antenna_cal_step(void) { cal_stage=(cal_stage+1)%5; return true; }
void antenna_cal_cancel(void) { cal_stage=0; }
unsigned antenna_cal_stage(void) { return cal_stage; }
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
void request_to_redraw(uint16_t m) { pending_redraw |= m; }
void draw_all(void) {
  if(pending_redraw==REDRAW_ALL) menu_visible=false;
  pending_redraw=0;
}
void menu_draw(uint32_t mask) {
  (void)mask; assert(ui_mode==UI_MENU && pending_redraw==0); menu_visible=true;
}
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
static freq_t keyboard_frequency;
freq_t keyboard_get_freq(void) { return keyboard_frequency; }
void ui_mode_keypad(int mode) { assert(mode==KM_ANT_CABLE_FREQ); ui_mode=UI_KEYPAD; }
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
    const uint8_t rgb[3]={pixels[y][x]?240:10,pixels[y][x]==3?40:(pixels[y][x]?240:14),pixels[y][x]==1?240:20};
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
  antenna_ui_restore(); assert(!antenna_ui_draw() && writes==0);
  antenna_ui_enter(0);
  assert(writes==1 && antenna_mode_decode(config._reserved));
  assert(frequency0==14000000 && frequency1==14350000 && sweep_points==101);
  assert(current_props._portz==50 && !(props_mode&DOMAIN_MODE) && sweep_mode==1);
  preview("waiting"); sample(); preview("sweep");
  // 20 m Extra CW/data ends at 14.150 MHz, 181 pixels into this 350 kHz span.
  for(unsigned y=264;y<269;y++) {
    assert(pixels[y][41]==0 && pixels[y][42]==3);
    assert(pixels[y][223]==3 && pixels[y][224]==0 && pixels[y][466]==0);
  }
  // A repeated frame must replace complete strips, without clearing the LCD,
  // changing pixels, or using a second framebuffer in firmware.
  uint8_t previous[320][480]; memcpy(previous,pixels,sizeof(pixels));
  unsigned transfers_before=strip_transfers;
  buffer_ready=false;
  assert(antenna_ui_draw());
  assert(strip_transfers==transfers_before+40 && next_strip==0);
  assert(memcmp(previous,pixels,sizeof(pixels))==0);
  assert(pixels[300][238]==1); // Filled RUNNING button.
  unsigned pause_updates=updates, pause_writes=writes;
  antenna_ui_touch(180,295); assert(!(sweep_mode&SWEEP_ENABLE));
  preview("paused"); assert(pixels[300][238]==0);
  assert(updates==pause_updates && writes==pause_writes);
  antenna_ui_touch(180,295); assert(sweep_mode&SWEEP_ENABLE);
  assert(updates==pause_updates); // Resume keeps the completed trace/range.
  uint32_t before=antenna_favorites_decode(config._reserved);
  antenna_ui_touch(350,10); assert(writes==2);
  assert(antenna_favorites_decode(config._reserved)==(before^(1u<<5)));
  fail_save=true; antenna_ui_touch(350,10); assert(messages==1);
  assert(antenna_favorites_decode(config._reserved)==(before^(1u<<5)));
  fail_save=false;
  unsigned filter_writes=writes;
  antenna_ui_touch(280,10); preview("all-bands");
  antenna_ui_lever(EVT_UP); assert(frequency0==18068000); // Next band, not next favorite.
  antenna_ui_lever(EVT_DOWN); assert(frequency0==14000000);
  antenna_ui_touch(280,10); preview("favorites-only");
  assert(writes==filter_writes+2); // Only the two band changes write, not filtering.
  antenna_ui_touch(260,295);
  assert(frequency0==frequency1 && sweep_points==21);
  uint32_t cw=frequency0; sample(); preview("tune");
  antenna_ui_touch(425,108); // TUNE keeps its own frequency controls.
  assert(frequency0==cw && frequency0==frequency1 && sweep_points==21);
  antenna_ui_lever(EVT_UP); assert(frequency0==cw+1750 && frequency0==frequency1);
  antenna_ui_lever(EVT_DOWN); assert(frequency0==cw && sweep_points==21);
  assert(pixels[266][42]==0); // No band rail in fixed-frequency tuning.
  antenna_ui_touch(400,200); assert(frequency0>cw && frequency0==frequency1);
  antenna_ui_touch(20,200); assert(frequency0==cw);
  for(unsigned i=0;i<300;i++) antenna_ui_touch(400,200);
  assert(frequency0==14350000);
  for(unsigned i=0;i<300;i++) antenna_ui_touch(20,200);
  assert(frequency0==14000000);
  antenna_ui_touch(400,295); assert(menus==0 && ui_mode==UI_NORMAL);
  preview("cal-profiles");
  antenna_ui_touch(100,120); // Empty HF cannot be deselected.
  antenna_ui_touch(100,210); // Empty V/U cannot be deselected.
  antenna_ui_touch(400,295); assert(cal_stage==1 && capture_mask==3);
  preview("cal-open");
  antenna_ui_touch(400,295); assert(cal_stage==2);
  preview("cal-short");
  antenna_ui_touch(400,295); assert(cal_stage==3);
  preview("cal-load");
  antenna_ui_touch(400,295); assert(cal_stage==4);
  preview("cal-save");
  antenna_ui_touch(400,295); assert(cal_stage==0);
  assert(frequency0==14000000 && frequency1==14350000 && sweep_points==101);
  assert(antenna_ui_draw());
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
  antenna_ui_touch(425,108); // VHF has no MULTI shortcut.
  assert(frequency0==144000000 && sweep_points==101);
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
  assert(pixels[266][42]==0); // Channel guides remain separate from HF regions.
  for(unsigned i=0;i<ANTENNA_60M_CHANNELS;i++) {
    unsigned x=42+(uint64_t)(antenna_60m_centers[i]-frequency0)*424/(frequency1-frequency0);
    assert(pixels[190][x]==1 && pixels[191][x]==1 && pixels[192][x]==0);
  }
  sample(); preview("60m-sweep");
  antenna_ui_touch(400,10);
  // Every band at maximum point count, graph ceiling, and large readout.
  sweep_points=401; antenna_ui_enter(0); assert(sweep_points==101);
  antenna_ui_touch(20,295);
  antenna_ui_touch(350,15); assert(sweep_points==201);
  preview("points-201");
  antenna_ui_touch(350,15); assert(sweep_points==401);
  antenna_ui_touch(350,15); assert(sweep_points==101);
  antenna_ui_touch(350,15); antenna_ui_touch(350,15);
  assert(sweep_points==401);
  antenna_ui_touch(400,295);
  for(unsigned g=0;g<2;g++) for(unsigned p=0;p<(g?1:2);p++)
    for(unsigned j=0;j<6;j++) {
      if(g && j==5) break;
      antenna_ui_touch(20,295); antenna_ui_touch(g*160+20,50);
      for(unsigned k=0;k<p;k++) antenna_ui_touch(180,295);
      antenna_ui_touch((j%2)*240+20,90+(j/2)*58);
      if(!g && p==1 && j>=5) { antenna_ui_touch(400,295); continue; }
      for(unsigned i=0;i<sweep_points;i++) measured[0][i][0]=1;
      antenna_ui_measurement(); assert(antenna_ui_draw());
      unsigned band_index=(g?ANTENNA_HF_COUNT:0)+p*6+j;
      if(band_index==4) {
        assert(pixels[266][42]==3 && pixels[266][466]==3);
        assert(pixels[266][467]==0); // All of 30 m is CW/data in the chart.
        preview("30m-bandplan");
      }
      if(band_index==1) preview("80m-bandplan");
    }
  antenna_ui_touch(400,10);
  assert(!antenna_mode_decode(config._reserved));
  antenna_favorites_encode(config._reserved,(1u<<3)|(1u<<5)|(1u<<13));
  antenna_ui_enter(0);
  unsigned saved_writes=writes;
  antenna_ui_lever(EVT_UP); assert(frequency0==7000000); // Wrap from 23 cm.
  antenna_ui_lever(EVT_DOWN); assert(frequency0==420000000);
  antenna_ui_lever(EVT_UP); assert(frequency0==7000000);
  antenna_ui_lever(EVT_UP); assert(frequency0==14000000);
  antenna_ui_lever(EVT_BUTTON_SINGLE_CLICK); assert(frequency0==14000000);
  antenna_ui_lever(EVT_DOWN); assert(frequency0==7000000); // Also leaves picker.
  antenna_ui_touch(260,295); assert(sweep_points==21);
  uint32_t tune_before=frequency0;
  antenna_ui_lever(EVT_UP); assert(frequency0==tune_before+1500 && frequency0==frequency1 && sweep_points==21);
  antenna_ui_touch(180,295); assert(frequency0==7000000 && frequency1==7300000 && sweep_points==401);
  antenna_ui_lever(EVT_UP); assert(frequency0==14000000 && sweep_points==401);
  assert(writes==saved_writes+6); // Six band changes; TUNE frequency changes do not write.

  // Simulate loading persisted configuration after a power cycle. Restoration
  // re-enters Antenna without writing flash; VNA stays VNA after leaving it.
  uint8_t boot_config[3]; memcpy(boot_config,config._reserved,3);
  antenna_ui_touch(400,10); assert(!antenna_ui_draw());
  assert(!antenna_mode_decode(config._reserved));
  unsigned boot_writes=writes;
  antenna_ui_restore(); assert(!antenna_ui_draw() && writes==boot_writes);
  memcpy(config._reserved,boot_config,3);
  antenna_ui_restore(); assert(antenna_ui_draw() && writes==boot_writes);
  assert(antenna_favorites_decode(config._reserved)==((1u<<3)|(1u<<5)|(1u<<13)));

  // A failed mode save must report the failure and restore the prior setting.
  fail_save=true; unsigned old_messages=messages;
  antenna_ui_touch(400,10);
  assert(!antenna_ui_draw() && antenna_mode_decode(config._reserved));
  assert(messages==old_messages+1);
  fail_save=false; antenna_ui_restore(); antenna_ui_touch(400,10);
  fail_save=true; old_messages=messages; antenna_ui_enter(0);
  assert(antenna_ui_draw() && !antenna_mode_decode(config._reserved));
  assert(messages==old_messages+1);
  fail_save=false; antenna_ui_touch(400,10);

  antenna_favorites_encode(config._reserved,0);
  antenna_ui_enter(0); uint32_t unchanged=frequency0; saved_writes=writes;
  antenna_ui_lever(EVT_UP); antenna_ui_lever(EVT_DOWN);
  assert(frequency0==unchanged && writes==saved_writes);
  antenna_ui_touch(280,10); antenna_ui_lever(EVT_UP);
  assert(frequency0!=unchanged && writes==saved_writes+1); // ALL works with no favorites.
  antenna_ui_lever(EVT_DOWN); assert(frequency0==unchanged);
  antenna_ui_touch(280,10);
  antenna_ui_touch(400,10);
  antenna_favorites_encode(config._reserved,1u<<3);
  antenna_ui_enter(0); antenna_ui_lever(EVT_UP); assert(frequency0==7000000);
  unsigned unchanged_updates=updates; saved_writes=writes;
  antenna_ui_lever(EVT_UP); antenna_ui_lever(EVT_DOWN);
  assert(updates==unchanged_updates && writes==saved_writes);
  // Eight-band overview. Completion retains the measured frequency range;
  // only the engine's protected preparation hook may advance it.
  antenna_ui_touch(20,295); antenna_ui_touch(20,15);
  assert(frequency0==3500000 && sweep_points==51);
  preview("multi-waiting");
  unsigned multi_writes=writes;
  const unsigned bands[]={1,3,4,5,6,7,8,9};
  for(unsigned k=0;k<8;k++) {
    assert(frequency0==antenna_bands[bands[k]].start);
    uint32_t f=frequency0;
    unsigned u=updates;
    antenna_ui_prepare_sweep(); assert(frequency0==f && updates==u); // No completed sweep yet.
    // Different synthetic curves make the preview useful; these are not RF measurements.
    for(unsigned i=0;i<sweep_points;i++) {
      float d=(float)i/(sweep_points-1)-(0.2f+0.07f*k);
      measured[0][i][0]=0.05f+0.02f*k+(1.0f+0.15f*k)*d*d;
      measured[0][i][1]=0;
    }
    antenna_ui_measurement(); assert(frequency0==f && updates==u);
    if(k==0) {
      preview("multi-first");
      memcpy(previous,pixels,sizeof pixels);
      antenna_ui_touch(320,15); assert(!(sweep_mode&SWEEP_ENABLE));
      antenna_ui_prepare_sweep(); assert(frequency0==f && updates==u);
      antenna_ui_touch(320,15); assert(sweep_mode&SWEEP_ENABLE);
    }
    preview("multi-progress");
    if(k>0) // Prior band's graph remains pixel-identical while other bands update.
      for(unsigned y=109;y<=173;y++) for(unsigned x=6;x<=112;x++)
        assert(pixels[y][x]==previous[y][x]);
    if(k<7) antenna_ui_prepare_sweep();
  }
  preview("multi-eight");
  assert(writes==multi_writes); // Automatic band scans never persist data or scan position.
  for(unsigned k=0;k<8;k++) {
    const antenna_band_t *b=&antenna_bands[bands[k]];
    unsigned x=(k%4)*120+6+(uint64_t)(b->cw_data_stop-b->start)*106/(b->stop-b->start);
    unsigned y=40+(k/4)*140, red=0;
    for(unsigned row=y+69;row<=y+133;row++) red+=pixels[row][x]==3;
    if(k==2) assert(red==0); // No CW boundary on 30 m.
    else assert(red>=50); // Trace may cross the guide, but cannot hide it.
  }
  antenna_ui_prepare_sweep(); assert(frequency0==3500000); // Wrap to 80 m.
  for(unsigned i=0;i<sweep_points;i++) { measured[0][i][0]=0; measured[0][i][1]=0; }
  measured[0][50][0]=0.5f;
  antenna_ui_measurement();
  want_min="MIN 1.00"; want_max="MAX 3.00"; saw_min=saw_max=false;
  preview("multi-extrema"); assert(saw_min && saw_max);
  want_min=want_max=NULL;
  for(unsigned i=0;i<sweep_points;i++) { measured[0][i][0]=1; measured[0][i][1]=0; }
  antenna_ui_measurement(); preview("multi-high");
  assert(pixels[109][50]==2); // SWR above 5 clips at the ceiling.
  antenna_ui_prepare_sweep(); assert(frequency0==7000000);
  uint32_t nan_bits=0x7fc00000;
  for(unsigned i=0;i<sweep_points;i++) memcpy(&measured[0][i][0],&nan_bits,4);
  antenna_ui_measurement(); preview("multi-invalid");
  for(unsigned y=110;y<173;y++) for(unsigned x=127;x<232;x++)
    assert(pixels[y][x]!=2); // Invalid samples must never join into a trace.
  frequency1++; unsigned u=updates;
  antenna_ui_prepare_sweep(); assert(updates==u); // Don't override USB's changed range.
  antenna_ui_touch(320,15); assert(frequency1==7300000);
  antenna_ui_lever(EVT_DOWN); antenna_ui_lever(EVT_BUTTON_SINGLE_CLICK);
  assert(frequency0==28000000 && sweep_points==401); // Opens selected 10 m detail.
  sample(); preview("multi-detail");
  antenna_ui_touch(425,108); // Direct HF detail -> overview, without visiting BANDS.
  assert(frequency0==3500000 && sweep_points==51);
  antenna_ui_touch(425,280); // 10 m card -> detail again.
  assert(frequency0==28000000 && sweep_points==401);
  antenna_ui_touch(20,295); antenna_ui_touch(20,15);
  preview("multi-reenter"); // Union cache cleared after single-band reuse.
  for(unsigned k=0;k<8;k++) {
    antenna_ui_touch((k%4)*120+60,40+(k/4)*140+100);
    assert(frequency0==antenna_bands[bands[k]].start && sweep_points==401);
    antenna_ui_touch(20,295); antenna_ui_touch(20,15);
  }
  antenna_ui_touch(430,15); // BANDS remains accessible without a bottom menu.
  antenna_ui_touch(400,295); antenna_ui_touch(400,10);
  assert(!antenna_ui_draw());
  // Restore every saved band independently of the last RAM selection. No old
  // measurement trace may survive; boot and fresh measurements never save data.
  for(unsigned v=0;v<=ANTENNA_VIEW_MULTI;v++) {
    antenna_view_encode(config._reserved,v);
    antenna_mode_encode(config._reserved,true);
    unsigned restored_writes=writes;
    antenna_ui_restore();
    assert(writes==restored_writes);
    unsigned b=v==ANTENNA_VIEW_MULTI?1:v;
    assert(frequency0==antenna_bands[b].start && frequency1==antenna_bands[b].stop);
    assert(antenna_ui_draw());
    for(unsigned y=109;y<173;y++) for(unsigned x=42;x<110;x++)
      assert(pixels[y][x]!=2); // No saved measurement curve.
    sample(); assert(writes==restored_writes);
    if(v==ANTENNA_VIEW_MULTI) antenna_ui_touch(60,140);
    antenna_ui_touch(400,10); assert(!antenna_ui_draw());
  }
  // Failed band saves retain the prior startup screen and report the error.
  antenna_view_encode(config._reserved,5); antenna_ui_enter(0);
  uint8_t prior_view[3]; memcpy(prior_view,config._reserved,3);
  fail_save=true; old_messages=messages;
  antenna_ui_lever(EVT_UP);
  assert(messages==old_messages+1 && memcmp(prior_view,config._reserved,3)==0);
  fail_save=false;

  // HF CAL defaults to the widest calibrated HF band, and requires saved CAL.
  antenna_ui_touch(400,10); antenna_view_encode(config._reserved,5); antenna_ui_enter(0);
  current_props.magic=0x414e5431; cal_status=CALSTAT_APPLY|CALSTAT_ED|CALSTAT_ES|CALSTAT_ER;
  electrical_delayS11=123e-9f;
  antenna_ui_touch(20,295); antenna_ui_touch(220,15);
  assert(antenna_view_decode(config._reserved)==ANTENNA_VIEW_CABLE);
  assert(frequency0==28000000 && frequency1==29700000 && sweep_points==401);
  assert(electrical_delayS11==0 && !(cal_status&CALSTAT_APPLY));
  cable_test_delay=2*30.48f/(SPEED_OF_LIGHT*0.66f);
  antenna_ui_measurement();
  want_min="30.48 m"; saw_min=false; preview("cable-no-hf-cal"); assert(!saw_min);
  saved_profiles[0]=true; antenna_ui_enter(0); antenna_ui_measurement();
  want_max="100.00 ft"; saw_min=saw_max=false;
  preview("cable-length"); assert(saw_min && saw_max); want_min=want_max=NULL;
  // Invalidating CAL while paused must also hide a cached calibrated result.
  cal_status &= ~CALSTAT_APPLY;
  want_min="30.48 m"; saw_min=false; preview("cable-cal-disabled"); assert(!saw_min);
  cal_status |= CALSTAT_APPLY; want_min=NULL;
  unsigned cable_writes=writes;
  antenna_ui_touch(320,295); assert(!(sweep_mode&SWEEP_ENABLE));
  antenna_ui_touch(150,180); assert(ui_mode==UI_KEYPAD && !antenna_ui_draw());
  button_t current; input_cable_frequency(0,&current); assert(current.p1.u==14200000);
  keyboard_frequency=7100000; input_cable_frequency(0,NULL); ui_mode_normal();
  assert(frequency0==7000000 && frequency1==7300000 && (sweep_mode&SWEEP_ENABLE));
  assert(antenna_cal_applied(0));
  want_min="30.48 m"; saw_min=false; preview("cable-new-range"); assert(!saw_min);
  antenna_ui_measurement();
  want_min="1/4: 6.97 m / 22.86 ft"; want_max="1/2: 13.93 m / 45.72 ft";
  saw_min=saw_max=false; preview("cable-targets"); assert(saw_min && saw_max);
  // Cancellation never invokes the callback, changes range, or resumes sweep.
  antenna_ui_touch(320,295); assert(!(sweep_mode&SWEEP_ENABLE));
  unsigned cable_updates=updates;
  antenna_ui_touch(150,180); ui_mode_normal();
  input_cable_frequency(0,&current); assert(current.p1.u==7100000);
  assert(updates==cable_updates && !(sweep_mode&SWEEP_ENABLE));
  // A covered V/U frequency automatically selects that profile, never HF CAL.
  keyboard_frequency=145000000; input_cable_frequency(0,NULL);
  assert(frequency0==144000000 && frequency1==148000000 && !(cal_status&CALSTAT_APPLY));
  want_min="30.48 m"; saw_min=false; antenna_ui_measurement();
  preview("cable-no-vu-cal"); assert(!saw_min);
  saved_profiles[1]=true; antenna_ui_enter(0); assert(antenna_cal_applied(1));
  antenna_ui_measurement(); preview("cable-vu-cal"); assert(saw_min);
  // Every preset selects its own covered range and HF/VU profile, including endpoints.
  for(unsigned b=0;b<ANTENNA_BAND_COUNT;b++) for(unsigned edge=0;edge<2;edge++) {
    keyboard_frequency=edge?antenna_bands[b].stop:antenna_bands[b].start;
    input_cable_frequency(0,NULL);
    assert(frequency0==antenna_bands[b].start && frequency1==antenna_bands[b].stop);
    assert(antenna_cal_applied(b>=10));
  }
  // Gaps use the explicit HF reference range; never calibrate across band gaps.
  keyboard_frequency=6000000; input_cable_frequency(0,NULL);
  assert(frequency0==28000000 && frequency1==29700000 && antenna_cal_applied(0));
  antenna_ui_touch(180,295); assert(frequency0==50000 && frequency1==5000000); // Skip FREQ CAL.
  keyboard_frequency=0; input_cable_frequency(0,NULL);
  input_cable_frequency(0,&current); assert(current.p1.u==50000);
  preview("cable-target-min");
  keyboard_frequency=UINT32_MAX; input_cable_frequency(0,NULL);
  input_cable_frequency(0,&current); assert(current.p1.u==FREQUENCY_MAX);
  preview("cable-target-max");
  keyboard_frequency=14200000; input_cable_frequency(0,NULL); antenna_ui_measurement();
  antenna_ui_touch(320,295); assert(!(sweep_mode&SWEEP_ENABLE));
  cable_updates=updates; want_min=want_max=NULL;
  antenna_ui_lever(EVT_UP); assert(velocity_factor==67);
  want_min="30.94 m"; want_max="1/4: 3.54 m / 11.60 ft"; saw_min=saw_max=false;
  preview("cable-vf-adjusted"); assert(saw_min && saw_max); want_min=want_max=NULL;
  antenna_ui_touch(360,130); assert(velocity_factor==66);
  for(unsigned i=0;i<110;i++) antenna_ui_touch(440,130);
  assert(velocity_factor==100);
  for(unsigned i=0;i<110;i++) antenna_ui_lever(EVT_DOWN);
  assert(velocity_factor==1);
  assert(writes==cable_writes && updates==cable_updates);
  velocity_factor=66;
  // Original wide resonance modes and ordinary CAL remain available.
  antenna_ui_touch(180,295); assert(frequency1==5000000 && !(cal_status&CALSTAT_APPLY));
  antenna_ui_touch(180,295); assert(frequency1==50000000 && (sweep_mode&SWEEP_ENABLE));
  cable_test_frequency=(SPEED_OF_LIGHT/400.0f)*66/30.48f;
  antenna_ui_measurement();
  want_min="30.48 m"; saw_min=false; preview("cable-wide"); assert(saw_min); want_min=NULL;
  frequency1=123456; electrical_delayS11=1e-9f;
  antenna_ui_measurement(); preview("cable-range-changed");
  antenna_ui_touch(320,295); assert(frequency1==50000000 && electrical_delayS11==0);
  antenna_ui_touch(420,295);
  assert(ui_mode==UI_MENU && menu_visible && !pending_redraw);
  assert(!antenna_ui_draw() && menus==1);
  current_props.magic=0; cal_frequency0=50000; cal_frequency1=50000000;
  cal_status=CALSTAT_APPLY|CALSTAT_ED|CALSTAT_ES|CALSTAT_ER;
  ui_mode_normal(); antenna_ui_measurement(); preview("cable-calibrated");
  antenna_ui_enter(0); assert(electrical_delayS11==0 && frequency1==50000000);
  current_props.magic=0x414e5431;
  want_min="30.48 m"; saw_min=false; antenna_ui_measurement();
  preview("cable-wrong-cal"); assert(!saw_min); want_min=NULL;
  cal_status &= ~CALSTAT_APPLY;
  cable_test_frequency=0; antenna_ui_measurement(); preview("cable-no-result");
  antenna_ui_touch(180,295); assert(frequency1==300000000);
  antenna_ui_touch(180,295); assert(frequency0==28000000 && antenna_cal_applied(0));
  // HF CAL opens the existing ANT walkthrough; no ordinary-slot calibration.
  antenna_ui_touch(420,295); preview("cable-ant-cal");
  antenna_ui_touch(300,295); assert(capture_mask==1 && cal_stage==1);
  antenna_ui_touch(30,295); assert(cal_stage==0 && frequency0==14000000);
  assert(electrical_delayS11==123e-9f);
  antenna_ui_touch(20,295); antenna_ui_touch(220,15);
  antenna_ui_touch(30,295); // BACK -> picker.
  assert(electrical_delayS11==123e-9f && frequency0==14000000);
  assert(antenna_view_decode(config._reserved)==5);
  antenna_ui_touch(220,15); antenna_ui_touch(420,15); // ANT -> band, then VNA.
  assert(antenna_ui_draw() && electrical_delayS11==123e-9f && frequency0==14000000);
  antenna_ui_touch(420,15); assert(!antenna_ui_draw());
  antenna_mode_encode(config._reserved,true); antenna_view_encode(config._reserved,ANTENNA_VIEW_CABLE);
  cable_writes=writes; antenna_ui_restore();
  assert(writes==cable_writes && frequency0==28000000 && frequency1==29700000 && sweep_points==401);
  preview("cable-restored");
  antenna_ui_touch(420,15); assert(antenna_ui_draw());
  antenna_ui_touch(420,15); assert(!antenna_ui_draw());
  puts("antenna UI transitions, mode persistence, favorites roller, bounds and rendering passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    tmp = Path(d)
    (tmp / 'nanovna.h').write_text(HEADER)
    for name in ('ui/core/ui_core.h', 'ui/core/ui_keypad.h', 'ui/core/ui_menu_engine.h', 'ui/menus/menu_cal.h', 'rf/analysis.h'):
        p = tmp / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#include "nanovna.h"\n')
    (tmp / 'harness.c').write_text(HARNESS)
    command = shlex.split(os.environ.get('HOST_CC', 'cc'))
    subprocess.run(command + ['-std=c11', '-Wall', '-Wextra', '-Werror', '-DNANOVNA_F303=1',
        '-I' + str(tmp), '-I' + str(ROOT / 'include'), str(tmp / 'harness.c'),
        str(ROOT / 'src/ui/antenna_ui.c'), str(ROOT / 'src/processing/antenna.c'),
        str(ROOT / 'src/processing/antenna_cal_grid.c'),
        str(ROOT / 'src/ui/resources/fonts/Font7x11b.c'), '-lm', '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
