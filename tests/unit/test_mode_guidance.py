"""Exercise production hardware-mode selection and saved startup confirmation."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/ui/menus/menu_system.c').read_text()
options=source[source.index('static const option_desc_t band_mode_options'):source.index('#ifndef NANOVNA_F303',source.index('// Band Mode Logic'))]
functions=source[source.index('static UI_FUNCTION_CALLBACK(menu_save_mode_cb)'):source.index('// RTC Logic')]
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NANOVNA_F303 1
#define VNA_MODE_RF_CONFIRMED_MASK 0x8000u
#define CALSTAT_APPLY 256
#define SI5351_BAND_SI5351 0
#define SI5351_BAND_MS5351 1
#define SI5351_BAND_SWC5351 2
#define SI5351_BAND_ZEETK 3
#define BUTTON_ICON_NONE 0
#define BUTTON_ICON_GROUP_CHECKED 1
#define BUTTON_ICON_GROUP 2
#define REDRAW_ALL 65535
#define UI_MENU 1
#define LCD_WIDTH 480
#define LCD_HEIGHT 320
#define MENU_BUTTON_WIDTH 91
#define LCD_FG_COLOR 1
#define LCD_BG_COLOR 0
#define MT_ADV_CALLBACK 1
#define MT_CALLBACK 2
#define MT_NEXT 3
#define UI_FUNCTION_CALLBACK(f) void f(uint16_t data)
#define UI_FUNCTION_ADV_CALLBACK(f) void f(uint16_t data,button_t *b)
typedef struct { int icon; union { const char *text; } p1; } button_t;
typedef struct { uint16_t value; const char *label; int icon; } option_desc_t;
typedef struct { int type,data; const char *label; const void *reference; } menuitem_t;
static struct { uint16_t _vna_mode; uint8_t _band_mode; } config, saved;
static uint16_t cal_status;
static unsigned ui_mode, mode, writes, redraws, panels;
static bool fail_save;
static uint8_t pixels[320][480];
extern const uint8_t x7x11b_bits[];
static const menuitem_t *current;
static const menuitem_t menu_back[]={{0,0,0,0}};
static int config_save(void) { writes++; if(fail_save)return -1; saved=config; return 0; }
static void ui_message_box(const char *a,const char *b,unsigned ms) {(void)a;(void)b;(void)ms;}
static void request_to_redraw(unsigned m) {(void)m;redraws++;}
static void si5351_set_band_mode(unsigned m) {mode=m;}
static const menuitem_t *current_menu_item(unsigned i) {(void)i;return current;}
static void menu_push_submenu(const menuitem_t *m) {current=m;}
static void ui_mode_menu(void) {ui_mode=UI_MENU;}
static void lcd_set_colors(unsigned a,unsigned b) {(void)a;(void)b;}
static void lcd_fill(int x,int y,int w,int h) {
  assert(x==0&&y==0&&w==389&&h==320);panels++;
  for(int j=y;j<y+h;++j) memset(pixels[j]+x,0,w);
}
static void lcd_drawstring_size(const char *s,int x,int y,unsigned scale) {
  assert(x>=0&&y>=0&&y+11*scale<=320);
  while(*s) {
    const uint8_t *g=x7x11b_bits+((uint8_t)*s++-0x16)*11;
    unsigned width=8-(g[0]&7);
    assert(x+width*scale<=480);
    for(unsigned r=0;r<11*scale;++r) for(unsigned c=0;c<width*scale;++c)
      pixels[y+r][x+c]=(g[r/scale]&(128>>(c/scale)))!=0;
    x+=width*scale;
  }
}
OPTIONS
FUNCTIONS
int main(void) {
  menu_mode_startup(); assert(menu_mode_help_active());
  menu_mode_help_draw(); assert(panels==1);
  const char *preview=getenv("MODE_PREVIEW_PATH");
  if(preview) {
    const char *labels[]={"DIRECT","DIV","MULTI","ZEETK","SAVE CONFIG","BACK"};
    for(unsigned i=0;i<6;++i) {
      unsigned y=1+i*36;
      for(unsigned x=389;x<480;++x) pixels[y][x]=pixels[y+34][x]=1;
      for(unsigned r=y;r<=y+34;++r) pixels[r][389]=pixels[r][479]=1;
      lcd_drawstring_size(labels[i],396,y+12,1);
    }
    FILE *f=fopen(preview,"wb"); assert(f); fprintf(f,"P6\n480 320\n255\n");
    for(unsigned y=0;y<320;++y) for(unsigned x=0;x<480;++x) {
      uint8_t rgb[3]={pixels[y][x]?240:10,pixels[y][x]?240:14,pixels[y][x]?240:20};
      fwrite(rgb,1,3,f);
    }
    fclose(f);
  }
  cal_status=CALSTAT_APPLY;
  menu_hardware_mode_acb(3,NULL); assert(mode==3 && config._band_mode==3);
  assert(!(config._vna_mode&VNA_MODE_RF_CONFIRMED_MASK) && !(cal_status&CALSTAT_APPLY));
  button_t b={0}; menu_hardware_mode_acb(3,&b); assert(b.icon==BUTTON_ICON_GROUP_CHECKED);
  menu_hardware_mode_acb(0,&b); assert(b.icon==BUTTON_ICON_GROUP);
  fail_save=true; menu_save_mode_cb(0);
  assert(!(config._vna_mode&VNA_MODE_RF_CONFIRMED_MASK));
  fail_save=false; menu_save_mode_cb(0); assert(saved._vna_mode&VNA_MODE_RF_CONFIRMED_MASK);
  config=saved; current=NULL; ui_mode=0;
  menu_mode_startup(); assert(current==NULL); // Confirmed boot does not open help.
  config._vna_mode=0; menu_mode_startup(); assert(menu_mode_help_active());
  menu_hardware_mode_open_acb(0,&b); assert(b.p1.text==band_mode_options[3].label);
  menu_hardware_mode_acb(2,NULL); assert(!(config._vna_mode&VNA_MODE_RF_CONFIRMED_MASK));
  assert(writes==2);
  puts("mode help: startup, selection, calibration invalidation, save failure and confirmation passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    tmp=Path(d); (tmp/'test.c').write_text(harness.replace('OPTIONS',options).replace('FUNCTIONS',functions))
    (tmp/'nanovna.h').write_text('#include <stdint.h>\n#define FONT_START_CHAR 0x16\n')
    cc=shlex.split(os.environ.get('HOST_CC','cc'))
    subprocess.run(cc+['-std=c11','-Wall','-Wextra','-Werror','-I'+str(tmp),str(tmp/'test.c'),
        str(ROOT/'src/ui/resources/fonts/Font7x11b.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
