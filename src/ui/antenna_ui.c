/* Large-label antenna workflow for the 480 x 320 H4 display. */
#include "nanovna.h"
#include "ui/antenna_ui.h"
#ifdef NANOVNA_F303
#include "processing/antenna.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_menu_engine.h"
#include "ui/menus/menu_cal.h"
#include <string.h>

static bool active, picker, tuning, valid;
static unsigned band = 9, group, page;
static uint32_t favorites;
static freq_t tune_frequency, expected_start, expected_stop;
static freq_t saved_start, saved_stop;
static uint16_t saved_points, saved_mode;
static uint8_t saved_sweep;
static float saved_portz;
/* CPU-only cache: CCM is not DMA-accessible. Written in full before valid=true;
 * this NOLOAD section is intentionally not initialized by the startup code. */
static float curve[SWEEP_POINTS_MAX] __attribute__((section(".ccm")));
static uint16_t count;
static antenna_result_t result;

static void redraw(void) { request_to_redraw(REDRAW_ALL); }
static void label(const char *s, int x, int y, unsigned scale) {
  lcd_drawstring_size(s, x, y, scale);
}
static void button(const char *s, int x, int y, int w, int h) {
  lcd_line(x,y,x+w-1,y); lcd_line(x,y+h-1,x+w-1,y+h-1);
  lcd_line(x,y,x,y+h-1); lcd_line(x+w-1,y,x+w-1,y+h-1);
  label(s,x+8,y+(h-22)/2,2);
}
static bool range_matches(void) {
  return frequency0 == expected_start && frequency1 == expected_stop &&
         sweep_points == (tuning ? 21 : saved_points) &&
         !(props_mode & DOMAIN_MODE) && current_props._portz == 50;
}
static void apply_range(void) {
  const antenna_band_t *b = &antenna_bands[band];
  expected_start = tuning ? tune_frequency : b->start;
  expected_stop = tuning ? tune_frequency : b->stop;
  frequency0 = expected_start; frequency1 = expected_stop;
  sweep_points = tuning ? 21 : saved_points;
  props_mode &= ~(DOMAIN_MODE | TD_CENTER_SPAN);
  current_props._portz = 50;
  sweep_mode |= SWEEP_ENABLE;
  valid = false;
  app_measurement_update_frequencies();
  redraw();
}
void antenna_ui_enter(uint16_t data) {
  (void)data;
  if (!active) {
    saved_start = frequency0; saved_stop = frequency1;
    saved_points = sweep_points; saved_mode = props_mode;
    saved_sweep = sweep_mode & SWEEP_ENABLE; saved_portz = current_props._portz;
    favorites = antenna_favorites_decode(config._reserved);
  }
  active = true; picker = false; tuning = false;
  ui_mode_normal(); apply_range();
}
static void leave(void) {
  active = false;
  frequency0 = saved_start; frequency1 = saved_stop; sweep_points = saved_points;
  props_mode = (props_mode & ~(DOMAIN_MODE | TD_CENTER_SPAN)) |
               (saved_mode & (DOMAIN_MODE | TD_CENTER_SPAN));
  current_props._portz = saved_portz;
  sweep_mode = (sweep_mode & ~SWEEP_ENABLE) | saved_sweep;
  app_measurement_update_frequencies(); redraw();
}
void antenna_ui_measurement(void) {
  if (!active || !range_matches()) { valid = false; return; }
  count = sweep_points;
  for (unsigned i = 0; i < count; ++i)
    curve[i] = antenna_swr(measured[0][i][0], measured[0][i][1]);
  result = antenna_analyze(curve, count);
  if (tuning) {
    result.minimum = count/2; result.swr = curve[count/2];
    result.valid = result.swr >= 1;
  }
  valid = result.valid;
}
static unsigned band_list(uint8_t *list) {
  unsigned n = 0;
  for (unsigned i = 0; i < ANTENNA_BAND_COUNT; ++i)
    if ((group == 0 && i < ANTENNA_HF_COUNT) ||
        (group == 1 && i >= ANTENNA_HF_COUNT) ||
        (group == 2 && (favorites & (1u << i)))) list[n++] = i;
  return n;
}
static void draw_picker(void) {
  uint8_t list[ANTENNA_BAND_COUNT];
  unsigned n = band_list(list);
  char s[48];
  label("SELECT BAND",12,8,2);
  button(group == 0 ? "[HF+6]" : "HF+6",0,38,160,42);
  button(group == 1 ? "[V/UHF]" : "V/UHF",160,38,160,42);
  button(group == 2 ? "[FAV]" : "FAV",320,38,160,42);
  if (!n) label("Use FAV on an antenna screen",12,130,2);
  for (unsigned j = 0; j < 6 && page*6+j < n; ++j) {
    unsigned i = list[page*6+j];
    plot_printf(s,sizeof(s),"%s%s",antenna_bands[i].name,(favorites & (1u<<i)) ? " *" : "");
    button(s,(j%2)*240,88+(j/2)*58,240,54);
  }
  button("PREV",0,276,120,44); button("NEXT",120,276,120,44);
  plot_printf(s,sizeof(s),"%u/%u",page+1,n ? (n+5)/6 : 1);
  label(s,258,288,2); button("BACK",360,276,120,44);
}
static int graph_y(float v) {
  if (v > 5) v = 5;
  return 245 - (int)((v-1)*20);
}
bool antenna_ui_draw(void) {
  if (!active || ui_mode != UI_NORMAL) return false;
  lcd_set_colors(LCD_FG_COLOR,LCD_BG_COLOR); lcd_clear_screen();
  if (picker) { draw_picker(); return true; }
  char s[80];
  label(antenna_bands[band].name,12,8,2);
  button((favorites & (1u<<band)) ? "FAV *" : "FAV +",250,0,130,36);
  button("VNA",380,0,100,36);
  const char *cal = "UNCALIBRATED";
  if (cal_status & CALSTAT_APPLY) {
    if (expected_start < cal_frequency0 || expected_stop > cal_frequency1)
      cal = "CAL: OUTSIDE SAVED RANGE";
    else if ((cal_status & (CALSTAT_ED|CALSTAT_ES|CALSTAT_ER)) !=
                         (CALSTAT_ED|CALSTAT_ES|CALSTAT_ER)) cal = "CAL: INCOMPLETE S11";
    else cal = (cal_status & CALSTAT_INTERPOLATED) ? "CAL: INTERPOLATED" : "CAL: APPLIED";
  }
  label(cal,12,42,1); label("50 ohm",386,42,1);
  label(tuning ? "LIVE SWR" : "MIN SWR",12,64,2);
  bool ready = valid && range_matches();
  if (!ready) label("--",12,90,5);
  else {
    if (result.swr >= 99.95f) strcpy(s,"99.9+");
    else plot_printf(s,sizeof(s),"%.2f",result.swr);
    label(s,12,90,5);
  }
  freq_t f = tuning ? tune_frequency : (ready ? get_frequency(result.minimum) : expected_start);
  plot_printf(s,sizeof(s),"%.5f MHz",(double)f/1000000.0);
  label(s,222,92,2);
  if (!range_matches()) label("Range changed: tap SWEEP",222,123,1);
  else if (!ready) label("Measuring...",222,123,1);
  else label(tuning ? "At selected frequency" : "Lowest sampled SWR",222,123,1);
  if (tuning) {
    button("- FREQ",16,173,190,58); button("+ FREQ",274,173,190,58);
    uint32_t step = (antenna_bands[band].stop-antenna_bands[band].start)/200;
    if (step < 10) step = 10;
    plot_printf(s,sizeof(s),"Jog or tap: %u Hz step",step);
    label(s,24,248,1);
  } else {
    lcd_line(42,165,42,245); lcd_line(42,245,466,245);
    lcd_line(42,225,466,225); label("2:1",4,220,1); label("5",18,164,1);
    lcd_set_foreground(LCD_TRACE_1_COLOR);
    if (ready && count > 1)
      for (unsigned i = 1; i < count; ++i)
        if (curve[i-1] >= 1 && curve[i] >= 1)
          lcd_line(42+(i-1)*424/(count-1),graph_y(curve[i-1]),
                   42+i*424/(count-1),graph_y(curve[i]));
    lcd_set_foreground(LCD_FG_COLOR);
    plot_printf(s,sizeof(s),"%.4f",(double)expected_start/1000000.0); label(s,42,250,1);
    plot_printf(s,sizeof(s),"%.4f MHz",(double)expected_stop/1000000.0); label(s,363,250,1);
    if (ready && result.bandwidth_valid) {
      float width = (result.high_index-result.low_index) *
                    ((float)(expected_stop-expected_start)/(count-1))/1000;
      plot_printf(s,sizeof(s),"2:1 %s%.1f kHz%s",
          (result.left_limited || result.right_limited) ? ">=" : "",(double)width,
          (result.left_limited || result.right_limited) ? " (edge)" : "");
    } else strcpy(s,"2:1 width: --");
    label(s,12,147,1);
  }
  button("BANDS",0,276,120,44); button("SWEEP",120,276,120,44);
  button("TUNE",240,276,120,44); button("CAL",360,276,120,44);
  return true;
}
static void tune_step(bool up) {
  const antenna_band_t *b = &antenna_bands[band];
  freq_t step = (b->stop-b->start)/200;
  if (step < 10) step = 10;
  if (up) tune_frequency = b->stop-tune_frequency < step ? b->stop : tune_frequency+step;
  else tune_frequency = tune_frequency-b->start < step ? b->start : tune_frequency-step;
  apply_range();
}
bool antenna_ui_touch(int x, int y) {
  if (!active) return false;
  touch_wait_release();
  if (picker) {
    uint8_t list[ANTENNA_BAND_COUNT]; unsigned n = band_list(list);
    if (y >= 38 && y < 80) { group = x/160; page = 0; }
    else if (y >= 88 && y < 258) {
      unsigned j = page*6 + ((y-88)/58)*2 + x/240;
      if (j < n) { band = list[j]; picker = false; tuning = false; apply_range(); }
    } else if (y >= 276) {
      if (x < 120 && page) --page;
      else if (x < 240 && x >= 120 && (page+1)*6 < n) ++page;
      else if (x >= 360) picker = false;
    }
  } else if (y < 36 && x >= 380) leave();
  else if (y < 36 && x >= 250) {
    uint8_t old[3]; memcpy(old,config._reserved,3);
    favorites ^= 1u<<band;
    antenna_favorites_encode(config._reserved,favorites);
    if (config_save() != 0) {
      memcpy(config._reserved,old,3); favorites = antenna_favorites_decode(old);
      ui_message_box("Favorites","Save failed",1000);
    }
  } else if (y >= 276) {
    if (x < 120) { picker = true; page = 0; }
    else if (x < 240) { tuning = false; apply_range(); }
    else if (x < 360) {
      if (!tuning) tune_frequency = valid && range_matches() ? get_frequency(result.minimum) :
                   antenna_bands[band].start+(antenna_bands[band].stop-antenna_bands[band].start)/2;
      tuning = true; apply_range();
    } else {
      // Calibrate the entire selected band, never just the tune frequency.
      tuning = false; apply_range(); ui_mode_menu(); menu_push_submenu(menu_cal_menu);
    }
  } else if (tuning && y >= 173 && y < 231) {
    if (x < 206) tune_step(false); else if (x >= 274) tune_step(true);
  }
  redraw(); return true;
}
bool antenna_ui_lever(uint16_t status) {
  if (!active) return false;
  if (status & EVT_BUTTON_SINGLE_CLICK) { picker = !picker; page = 0; }
  else if (tuning && !picker && (status & (EVT_UP|EVT_DOWN))) tune_step((status & EVT_UP) != 0);
  else if (picker) {
    uint8_t list[ANTENNA_BAND_COUNT]; unsigned n = band_list(list);
    if ((status & EVT_UP) && (page+1)*6 < n) ++page;
    if ((status & EVT_DOWN) && page) --page;
  }
  redraw(); return true;
}
#endif
