/* Large-label antenna workflow for the 480 x 320 H4 display. */
#include "nanovna.h"
#include "ui/antenna_ui.h"
#ifdef NANOVNA_F303
#pragma GCC optimize("Os")
#include "processing/antenna.h"
#include "processing/antenna_cal.h"
#include "ui/core/ui_core.h"
#include "ui/core/ui_keypad.h"
#include "ui/core/ui_menu_engine.h"
#include "ui/menus/menu_cal.h"
#include "ui/draw/traces.h"
#include "rf/analysis.h"
#include <string.h>

static bool active, picker, tuning, valid;
static bool cal_page;
static bool cable;
// HF reference (10 m band), target-frequency band, then wide resonance ranges.
static uint8_t cable_range;
static const freq_t cable_stops[] = {29700000,0,5000000,50000000,300000000};
static float cable_old_delay;
static freq_t cable_target = 14200000;
static bool multi, multi_next;
static uint8_t multi_scan, multi_selected;
#define MULTI_POINTS 51
static const uint8_t multi_bands[] = {1,3,4,5,6,7,8,9};
static uint8_t cal_mask;
static unsigned band = 5, group, page;
static uint32_t favorites;
static uint32_t load_favorites(void) {
  return antenna_favorites_decode(config._reserved) |
    ((config._vna_mode & VNA_MODE_EU868_FAVORITE_MASK) ? 1u<<ANTENNA_EU868_BAND : 0);
}
static bool favorites_only = true;
static freq_t tune_frequency, expected_start, expected_stop;
static uint16_t antenna_points = 101;
// Automatic MULTI scans do not count as a deliberate band selection.
static unsigned vna_band = ANTENNA_BAND_COUNT;
static struct {
  freq_t start, stop, cal_start, cal_stop;
  float portz, load_r;
  uint32_t magic, reserved[5];
  const properties_t *cal;
  marker_t marker[MARKERS_MAX];
  uint16_t points, mode, cal_points, cal_flags, slot;
  uint8_t power, old_cal_power, sweep;
} saved __attribute__((section(".ccm")));
_Static_assert(sizeof saved <= 148, "VNA return state exceeds spare CCM");
/* CPU-only cache: CCM is not DMA-accessible. Written in full before valid=true;
 * this NOLOAD section is intentionally not initialized by the startup code. */
static union {
  float cable_delay;
  struct {
    float curve[SWEEP_POINTS_MAX];
    char impedance[24];
  } single;
  struct {
    uint8_t y[8][MULTI_POINTS]; // 0..64 graph coordinates; 255 means invalid.
    float low[8], high[8];
    uint8_t ready, calibrated, incomplete;
  } overview;
} cache __attribute__((section(".ccm")));
#define curve cache.single.curve
static uint16_t count;
static antenna_result_t result;

/* Compose complete strips before transferring them. Clearing the LCD and then
 * drawing individual glyphs/lines exposes a blank frame on every sweep.
 * Reuse the shared SPI buffer; a full framebuffer would not fit in H4 RAM. */
#define STRIP_HEIGHT 8
_Static_assert(LCD_WIDTH * STRIP_HEIGHT <= SPI_BUFFER_SIZE, "antenna strip exceeds SPI buffer");
static int strip_y;
static pixel_t ink, paper;

static void pixel(int x, int y, pixel_t color) {
  if (x >= 0 && x < LCD_WIDTH && y >= strip_y && y < strip_y + STRIP_HEIGHT)
    spi_buffer[(y-strip_y)*LCD_WIDTH+x] = color;
}
static void line(int x, int y, int x1, int y1) {
  if ((y < strip_y && y1 < strip_y) ||
      (y >= strip_y+STRIP_HEIGHT && y1 >= strip_y+STRIP_HEIGHT)) return;
  int dx = x1-x, dy = y1-y;
  int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
  if (dx < 0) dx = -dx;
  if (dy > 0) dy = -dy;
  int error = dx+dy;
  for (;;) {
    pixel(x,y,ink);
    if (x == x1 && y == y1) break;
    int twice = 2*error;
    if (twice >= dy) { error += dy; x += sx; }
    if (twice <= dx) { error += dx; y += sy; }
  }
}
static void redraw(void) { request_to_redraw(REDRAW_ALL); }
static void apply_range(void);
static unsigned cable_target_band(void) {
  for (unsigned i = 0; i < ANTENNA_BAND_COUNT; ++i)
    if (cable_target >= antenna_bands[i].start && cable_target <= antenna_bands[i].stop)
      return i;
  return ANTENNA_BAND_COUNT;
}
UI_KEYBOARD_CALLBACK(input_cable_frequency) {
  (void)data;
  if (b) { b->p1.u = cable_target; return; }
  freq_t f = keyboard_get_freq();
  if (f < 50000) f = 50000;
  if (f > FREQUENCY_MAX) f = FREQUENCY_MAX;
  cable_target = f;
  vna_band = band;
  cable_range = cable_target_band() < ANTENNA_BAND_COUNT ? 1 : 0;
  apply_range();
}
static void label(const char *s, int x, int y, unsigned scale) {
  if (y >= strip_y+STRIP_HEIGHT || y+(int)(FONT_GET_HEIGHT*scale) <= strip_y) return;
  while (*s) {
    unsigned ch = (uint8_t)*s++;
    const uint8_t *glyph = FONT_GET_DATA(ch);
    unsigned width = FONT_GET_WIDTH(ch);
    for (int row = 0; row < FONT_GET_HEIGHT*(int)scale; ++row) {
      if (y+row < strip_y || y+row >= strip_y+STRIP_HEIGHT) continue;
      for (unsigned col = 0; col < width*scale; ++col)
        pixel(x+col,y+row,(glyph[row/scale] & (0x80 >> (col/scale))) ? ink : paper);
    }
    x += width*scale;
  }
}
static void button(const char *s, int x, int y, int w, int h) {
  line(x,y,x+w-1,y); line(x,y+h-1,x+w-1,y+h-1);
  line(x,y,x,y+h-1); line(x+w-1,y,x+w-1,y+h-1);
  label(s,x+8,y+(h-22)/2,2);
}
static bool range_matches(void) {
  return frequency0 == expected_start && frequency1 == expected_stop &&
         sweep_points == (cable ? 401 : multi ? MULTI_POINTS : tuning ? 21 : antenna_points) &&
         (!cable || electrical_delayS11 == 0) &&
         !(props_mode & DOMAIN_MODE) && current_props._portz == 50;
}
static bool cable_calibrated(void) {
  return (cable_range < 2 ? antenna_cal_applied(cable_range == 1 && cable_target_band() >= 10) :
                          current_props.magic != ANT_CAL_MAGIC) &&
    (cal_status & (CALSTAT_APPLY|CALSTAT_ED|CALSTAT_ES|CALSTAT_ER)) ==
                 (CALSTAT_APPLY|CALSTAT_ED|CALSTAT_ES|CALSTAT_ER) &&
    cal_frequency0 <= expected_start && cal_frequency1 >= expected_stop &&
    (config._vna_mode & VNA_MODE_RF_CONFIRMED_MASK);
}
static void apply_range(void) {
  unsigned cal_band = cable_range == 1 ? cable_target_band() : 9;
  const antenna_band_t *b = &antenna_bands[cable && cable_range < 2 ? cal_band : band];
  expected_start = cable ? (cable_range < 2 ? b->start : 50000) : tuning ? tune_frequency : b->start;
  expected_stop = cable ? (cable_range < 2 ? b->stop : cable_stops[cable_range]) : tuning ? tune_frequency : b->stop;
  frequency0 = expected_start; frequency1 = expected_stop;
  sweep_points = cable ? 401 : multi ? MULTI_POINTS : tuning ? 21 : antenna_points;
  props_mode &= ~(DOMAIN_MODE | TD_CENTER_SPAN);
  current_props._portz = 50;
  if (cable) {
    electrical_delayS11 = 0;
    if (cable_range < 2 && (vna_band < ANTENNA_BAND_COUNT || saved.cal) &&
        !antenna_cal_load(cal_band >= 10)) cal_status &= ~CALSTAT_APPLY;
    // Phase measurements stay within a covered band; wide sweeps need ordinary CAL.
    if (!cable_calibrated()) cal_status &= ~CALSTAT_APPLY;
  } else if (vna_band < ANTENNA_BAND_COUNT || saved.cal)
    antenna_cal_load(band >= 10);
  // If the previous VNA calibration is unsaved, retain its coefficients until
  // a deliberate band/tuning action. There is no RAM for a second full table.
  sweep_mode |= SWEEP_ENABLE;
  valid = false; multi_next = false;
  app_measurement_update_frequencies();
  redraw();
}
static void multi_enter(void);
static void cable_enter(void) {
  cable_old_delay = electrical_delayS11;
  cable = true; picker = tuning = multi = false;
  if (velocity_factor < 1 || velocity_factor > 100) velocity_factor = 66;
  apply_range();
}
static void cable_leave(void) {
  cable = false;
  electrical_delayS11 = cable_old_delay;
}
static void save_mode(bool enabled) {
  unsigned view = cable ? ANTENNA_VIEW_CABLE : multi ? ANTENNA_VIEW_MULTI :
                  band == ANTENNA_EU868_BAND ? ANTENNA_VIEW_EU868 : band;
  if (antenna_mode_decode(config._reserved) == enabled &&
      (!enabled || antenna_view_decode(config._reserved) == view)) return;
  uint8_t old[3]; memcpy(old,config._reserved,3);
  antenna_mode_encode(config._reserved,enabled);
  if (enabled) antenna_view_encode(config._reserved,view);
  if (config_save() != 0) {
    memcpy(config._reserved,old,3);
    ui_message_box("Startup view","Save failed",1000);
  }
}
void antenna_ui_enter(uint16_t data) {
  if (!active) {
    saved.start = frequency0; saved.stop = frequency1;
    saved.points = sweep_points; saved.mode = props_mode;
    saved.sweep = sweep_mode; saved.portz = current_props._portz;
    saved.magic = current_props.magic; saved.power = current_props._power;
    saved.cal_start = cal_frequency0; saved.cal_stop = cal_frequency1;
    saved.cal_points = cal_sweep_points; saved.cal_flags = cal_status;
    saved.old_cal_power = cal_power; saved.load_r = cal_load_r; saved.slot = lastsaveid;
    memcpy(saved.marker,markers,sizeof saved.marker);
    memcpy(saved.reserved,current_props._reserved1,sizeof saved.reserved);
    saved.cal = get_properties(lastsaveid);
    if (saved.cal && memcmp(saved.cal->_cal_data,cal_data,sizeof cal_data)) saved.cal = NULL;
    favorites = load_favorites();
  }
  unsigned view = antenna_view_decode(config._reserved);
  unsigned view_band = view == ANTENNA_VIEW_EU868 ? ANTENNA_EU868_BAND : view;
  if (!active || data)
    vna_band = data && view != ANTENNA_VIEW_MULTI && view != ANTENNA_VIEW_CABLE ? view_band : ANTENNA_BAND_COUNT;
  if (cable) cable_leave();
  active = true; picker = false; tuning = false; multi = false;
  ui_mode_normal();
  if (view == ANTENNA_VIEW_MULTI) multi_enter();
  else if (view == ANTENNA_VIEW_CABLE) cable_enter();
  else { band = view_band; apply_range(); }
  save_mode(true);
}
void antenna_ui_restore(void) {
  // Configuration and the normal UI/plot must be initialized first. Restoring
  // the already-saved mode must not write flash on every power cycle.
  if (antenna_mode_decode(config._reserved)) antenna_ui_enter(1);
}
static void leave(void) {
  if (cable) cable_leave();
  active = false;
  if (vna_band < ANTENNA_BAND_COUNT) {
    const antenna_band_t *b = &antenna_bands[vna_band];
    freq_t f = band == vna_band && !multi && valid && range_matches() ?
               get_frequency(result.minimum) : b->start+(b->stop-b->start)/2;
    band = vna_band; multi = tuning = picker = false; apply_range();
    props_mode &= ~(TD_MARKER_TRACK | TD_MARKER_DELTA);
    for (unsigned i=0;i<TRACES_MAX;i++) set_trace_enable(i,i<2);
    set_trace_type(0,TRC_SMITH,0); set_trace_scale(0,1); set_trace_refpos(0,0);
    trace[0].smith_format = MS_RX;
    set_trace_type(1,TRC_SWR,0); set_trace_scale(1,0.5f); set_trace_refpos(1,0);
    set_active_trace(0); current_props._measure = MEASURE_NONE;
    for (unsigned i=0;i<MARKERS_MAX;i++) markers[i].enabled = i==0;
    markers[0].frequency = f; active_marker = 0;
  } else {
    frequency0 = saved.start; frequency1 = saved.stop; sweep_points = saved.points;
    props_mode = saved.mode; current_props._portz = saved.portz; sweep_mode = saved.sweep;
    current_props.magic = saved.magic; current_props._power = saved.power;
    cal_frequency0 = saved.cal_start; cal_frequency1 = saved.cal_stop;
    cal_sweep_points = saved.cal_points; cal_status = saved.cal_flags;
    cal_power = saved.old_cal_power; cal_load_r = saved.load_r; lastsaveid = saved.slot;
    memcpy(markers,saved.marker,sizeof saved.marker);
    memcpy(current_props._reserved1,saved.reserved,sizeof saved.reserved);
    if (saved.cal) memcpy(cal_data,saved.cal->_cal_data,sizeof cal_data);
  }
  app_measurement_update_frequencies(); redraw();
  save_mode(false);
}
// Changing band in the completion callback would relabel a snapshot that USB
// readers have not yet consumed. The engine calls this only after owning it.
void antenna_ui_prepare_sweep(void) {
  if (!active || !multi || picker || !multi_next || !range_matches() ||
      !(sweep_mode & SWEEP_ENABLE)) return;
  multi_next = false;
  multi_scan = (multi_scan+1)%8;
  band = multi_bands[multi_scan];
  apply_range();
}
static void multi_enter(void) {
  multi = true; picker = tuning = multi_next = false;
  multi_scan = multi_selected = 0;
  memset(&cache,0,sizeof cache);
  band = multi_bands[0]; apply_range();
}
static void multi_open(unsigned card) {
  vna_band = band = multi_bands[card]; multi = multi_next = false; apply_range();
}
void antenna_ui_measurement(void) {
  if (!active || !range_matches()) { valid = false; return; }
  if (cable) {
    if (cable_range < 2) cache.cable_delay = cable_calibrated() ? cable_phase_delay() : 0;
    else {
      float f = !(cal_status & CALSTAT_APPLY) || cable_calibrated() ?
                cable_quarter_wave_frequency() : 0;
      cache.cable_delay = f > 0 ? 0.5f/f : 0;
    }
    valid = cache.cable_delay > 0;
    return;
  }
  if (multi) {
    unsigned k = multi_scan, bit = 1u<<k;
    float low = 1000, high = 0;
    cache.overview.incomplete &= ~bit;
    for (unsigned i=0; i<MULTI_POINTS; ++i) {
      float v = antenna_swr(measured[0][i][0],measured[0][i][1]);
      cache.overview.y[k][i] = 255;
      if (v < 1) { cache.overview.incomplete |= bit; continue; }
      if (v < low) low = v;
      if (v > high) high = v;
      cache.overview.y[k][i] = v > 5 ? 0 : 64-(int)((v-1)*16);
    }
    cache.overview.low[k] = low; cache.overview.high[k] = high;
    cache.overview.ready |= bit;
    cache.overview.calibrated &= ~bit;
    if ((cal_status & (CALSTAT_APPLY|CALSTAT_ED|CALSTAT_ES|CALSTAT_ER)) ==
                     (CALSTAT_APPLY|CALSTAT_ED|CALSTAT_ES|CALSTAT_ER) &&
        expected_start >= cal_frequency0 && expected_stop <= cal_frequency1 &&
        (current_props.magic != ANT_CAL_MAGIC ||
         antenna_cal_range(current_props._reserved1[0],expected_start,expected_stop)) &&
        (config._vna_mode & VNA_MODE_RF_CONFIRMED_MASK)) cache.overview.calibrated |= bit;
    multi_next = true;
    return;
  }
  count = sweep_points;
  for (unsigned i = 0; i < count; ++i)
    curve[i] = antenna_swr(measured[0][i][0], measured[0][i][1]);
  result = antenna_analyze(curve, count);
  if (tuning) {
    result.minimum = count/2; result.swr = curve[count/2];
    result.valid = result.swr >= 1;
  }
  // Capture Z with the completed trace, before the next sweep overwrites S11.
  // Format once per capture, not once per LCD strip.
  strcpy(cache.single.impedance,"Z --");
  if (result.valid) {
    const float *v = measured[0][result.minimum];
    float re = 1-v[0], d = re*re+v[1]*v[1];
    // Avoid a singular/overflowing conversion (especially with fast math).
    // This excludes only extreme Z or unusable S11, outside our display range.
    if (d >= 1e-8f && d <= 1e8f) {
      float r = resistance(result.minimum,v), x = reactance(result.minimum,v);
      if (r > -10000 && r < 10000 && x > -10000 && x < 10000)
        plot_printf(cache.single.impedance,sizeof cache.single.impedance,
                    "Z %.1f%+j.1f" S_OHM,r,x);
    }
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
  button("MULTI HF",0,0,170,36);
  button("CABLE",170,0,130,36);
  plot_printf(s,sizeof(s),"PTS: %u",antenna_points);
  button(s,300,0,180,36);
  button(group == 0 ? "[HF+6]" : "HF+6",0,38,160,42);
  button(group == 1 ? "[V/UHF]" : "V/UHF",160,38,160,42);
  button(group == 2 ? "[FAV]" : "FAV",320,38,160,42);
  if (!n) label("Use + on an antenna screen",12,130,2);
  for (unsigned j = 0; j < 6 && page*6+j < n; ++j) {
    unsigned i = list[page*6+j];
    plot_printf(s,sizeof(s),"%s%s",antenna_bands[i].name,(favorites & (1u<<i)) ? " *" : "");
    int x = (j%2)*240, y = 88+(j/2)*58;
    button("",x,y,240,54);
    label(s,x+8,y+16,2);
    uint32_t mhz = i == ANTENNA_EU868_BAND ? 868 :
      ((uint64_t)antenna_bands[i].start+antenna_bands[i].stop+1000000)/2000000;
    plot_printf(s,sizeof(s),"%u MHz",mhz);
    unsigned width = 0;
    for (const char *p = s; *p; ++p) width += FONT_GET_WIDTH((uint8_t)*p)*2;
    label(s,x+232-width,y+16,2);
  }
  button("PREV",0,276,120,44); button("NEXT",120,276,120,44);
  plot_printf(s,sizeof(s),"%u/%u",page+1,n ? (n+5)/6 : 1);
  label(s,258,288,2); button("BACK",360,276,120,44);
}
static int graph_y(float v) {
  if (v > 5) v = 5;
  return 245 - (int)((v-1)*20);
}
static unsigned required_profiles(void) {
  unsigned mask=0;
  for (unsigned p=0;p<2;++p) {
    if (!antenna_cal_saved(p)) mask |= 1u<<p;
  }
  return mask;
}
static void draw_cal(void) {
  unsigned stage=antenna_cal_stage();
  label("ANTENNA CAL",12,12,3);
  if (!stage) {
    label("401 points in each profile",12,60,2);
    unsigned required=required_profiles();
    button((cal_mask & 1) ? "HF ANT: CAPTURE" : "HF ANT: KEEP",12,100,456,50);
    label((required & 1) ? "Needs ANT CAL - included" : "Saved - tap to toggle",24,156,1);
    button((cal_mask & 2) ? "V/U ANT: CAPTURE" : "V/U ANT: KEEP",12,182,456,50);
    label((required & 2) ? "Needs ANT CAL - included" : "Saved - tap to toggle",24,238,1);
  } else {
    label(stage==1 ? "Connect OPEN" : stage==2 ? "Connect SHORT" :
          stage==3 ? "Connect 50 ohm LOAD" : "Ready to save",12,82,3);
    label("At the antenna cable end",12,136,2);
    label(cal_mask==3 ? "HF + V/U: one connection" : (cal_mask&1) ? "HF ANT" : "V/U ANT",12,181,2);
    label(stage==4 ? "Replaces selected ANT slots" : "Then tap CAPTURE below",12,230,2);
  }
  button("CANCEL",0,276,200,44);
  button(!stage ? "START" : stage==4 ? "SAVE" : "CAPTURE",200,276,280,44);
}
static void draw_multi(void) {
  char s[40];
  label("HF MULTI",8,3,2);
  label("51 PTS  MHz  SWR 1-5",8,26,1);
  button("VNA",180,0,100,38);
  button((sweep_mode & SWEEP_ENABLE) ? "PAUSE" : "RUN",280,0,100,38);
  button("BANDS",380,0,100,38);
  for (unsigned k=0; k<8; ++k) {
    int x=(k%4)*120, y=40+(k/4)*140;
    if (y >= strip_y+STRIP_HEIGHT || y+140 <= strip_y) continue;
    const antenna_band_t *b=&antenna_bands[multi_bands[k]];
    unsigned bit=1u<<k;
    ink=GET_PALTETTE_COLOR(k==multi_selected ? LCD_TRACE_1_COLOR : LCD_FG_COLOR);
    line(x,y,x+119,y); line(x,y,x,y+139);
    line(x+119,y,x+119,y+139); line(x,y+139,x+119,y+139);
    label(k ? b->name : "80m",x+6,y+5,2);
    ink=GET_PALTETTE_COLOR(LCD_FG_COLOR);
    // CAL refers to this cached band, not the currently sweeping band.
    label((cache.overview.ready & bit) ?
          ((cache.overview.calibrated & bit) ? "CAL" : "UNCAL") : "--",x+83,y+8,1);
    plot_printf(s,sizeof(s),"%.3f-%.3f",(double)b->start/1000000,(double)b->stop/1000000);
    label(s,x+6,y+28,1);
    for (unsigned n=0;n<2;++n) {
      float v=n ? cache.overview.high[k] : cache.overview.low[k];
      const char *name=n ? "MAX" : "MIN";
      if (!(cache.overview.ready & bit) || !cache.overview.high[k])
        plot_printf(s,sizeof(s),"%s --",name);
      else if (v>=99.95f) plot_printf(s,sizeof(s),"%s 99.9+",name);
      else plot_printf(s,sizeof(s),"%s %.2f",name,(double)v);
      label(s,x+6,y+42+n*12,1);
    }
    if (cache.overview.incomplete & bit) label("!",x+106,y+54,1);
    if (k==multi_scan && (sweep_mode & SWEEP_ENABLE) && range_matches())
      label(">",x+106,y+42,1);
    // Identical scales make band shapes comparable. Dashed lines mark 3:1,2:1.
    for (int d=0;d<=106;d+=5) {
      line(x+6+d,y+101,x+7+d,y+101);
      line(x+6+d,y+117,x+7+d,y+117);
    }
    line(x+6,y+69,x+6,y+133); line(x+6,y+133,x+112,y+133);
    if (b->cw_data_stop && multi_bands[k] != 4) {
      int cx=x+6+(uint64_t)(b->cw_data_stop-b->start)*106/(b->stop-b->start);
      ink=GET_PALTETTE_COLOR(LCD_TRACE_5_COLOR);
      line(cx,y+69,cx,y+133);
    }
    ink=GET_PALTETTE_COLOR(LCD_TRACE_1_COLOR);
    if (cache.overview.ready & bit)
      for (unsigned i=1;i<MULTI_POINTS;++i) {
        unsigned a=cache.overview.y[k][i-1], b_y=cache.overview.y[k][i];
        if (a<=64 && b_y<=64)
          line(x+6+(i-1)*106/(MULTI_POINTS-1),y+69+a,
               x+6+i*106/(MULTI_POINTS-1),y+69+b_y);
      }
    ink=GET_PALTETTE_COLOR(LCD_FG_COLOR);
  }
}
static void draw_cable(void) {
  char s[48];
  bool calibrated = cable_calibrated();
  label("CABLE LENGTH",12,8,3);
  button("ANT",380,0,100,44);
  if (cable_range < 2) {
    unsigned b = cable_range ? cable_target_band() : 9;
    plot_printf(s,sizeof s,"%s %s | OPEN end",calibrated ? "ANT CAL:" : "NEEDS ANT CAL:",antenna_bands[b].name);
    label(s,12,48,1);
  } else label(calibrated ? "OPEN end | CAL: S11 APPLIED" : "OPEN end | UNCAL: CAL at CH0",12,48,1);
  if (valid && range_matches() && (cable_range >= 2 || calibrated)) {
    float metres = (SPEED_OF_LIGHT/200.0f)*velocity_factor*cache.cable_delay;
    plot_printf(s,sizeof s,"%.2f m",metres); label(s,12,72,4);
    plot_printf(s,sizeof s,"%.2f ft",metres/0.3048f); label(s,12,124,3);
  } else {
    label("-- m / -- ft",12,80,3);
    label("Check CAL/end; try wider range",12,124,1);
  }
  plot_printf(s,sizeof s,"VF %u%%",velocity_factor);
  label(s,338,76,2);
  button("-",338,110,60,44); button("+",408,110,60,44);
  plot_printf(s,sizeof s,"FREQ %.6f MHz",(double)cable_target/1000000.0);
  button(s,12,166,310,38);
  label("Roller: VF",338,180,1);
  float target = (SPEED_OF_LIGHT/400.0f)*velocity_factor/cable_target;
  for (unsigned i = 0; i < 2; ++i, target *= 2) {
    plot_printf(s,sizeof s,"1/%u: %.2f m / %.2f ft",i ? 2 : 4,target,target/0.3048f);
    label(s,12,214+30*i,2);
  }
  button("BACK",0,276,100,44);
  plot_printf(s,sizeof s,"%u MHz",cable_stops[cable_range]/1000000);
  button(cable_range < 2 ? (cable_range ? "FREQ CAL" : "HF CAL") : s,100,276,160,44);
  button((sweep_mode & SWEEP_ENABLE) ? "PAUSE" : "RUN",260,276,120,44);
  button("CAL",380,276,100,44);
}
static void draw_strip(void) {
  if (cable) { draw_cable(); return; }
  if (cal_page) { draw_cal(); return; }
  if (picker) { draw_picker(); return; }
  if (multi) { draw_multi(); return; }
  char s[80];
  label(antenna_bands[band].name,12,8,2);
  plot_printf(s,sizeof(s),"%u PTS",tuning ? 21 : antenna_points);
  label(s,130,8,2);
  button(favorites_only ? "FAV" : "ALL",250,0,85,36);
  button((favorites & (1u<<band)) ? "*" : "+",335,0,45,36);
  button("VNA",380,0,100,36);
  const char *cal = "UNCALIBRATED";
  if (cal_status & CALSTAT_APPLY) {
    if (expected_start < cal_frequency0 || expected_stop > cal_frequency1)
      cal = "CAL: OUTSIDE SAVED RANGE";
    else if ((cal_status & (CALSTAT_ED|CALSTAT_ES|CALSTAT_ER)) !=
                         (CALSTAT_ED|CALSTAT_ES|CALSTAT_ER)) cal = "CAL: INCOMPLETE S11";
    else if (current_props.magic == ANT_CAL_MAGIC)
      cal = antenna_cal_range(current_props._reserved1[0],expected_start,expected_stop) ?
            (band<10 ? "CAL: HF ANT" : "CAL: V/U ANT") : "CAL: OUTSIDE ANT BANDS";
    else cal = (cal_status & CALSTAT_INTERPOLATED) ? "CAL: INTERPOLATED" : "CAL: APPLIED";
  }
  if (!(config._vna_mode & VNA_MODE_RF_CONFIRMED_MASK)) cal = "CONFIRM HARDWARE MODE";
  label(cal,12,42,1);
  // Small grooved roller and its current left/right function.
  line(390,46,393,43); line(390,46,393,49);
  line(396,40,406,40); line(396,52,406,52);
  line(396,40,396,52); line(406,40,406,52);
  line(399,42,399,50); line(403,42,403,50);
  line(412,46,409,43); line(412,46,409,49);
  label(tuning ? "FREQ +/-" : (favorites_only ? "BAND FAV" : "BAND ALL"),418,42,1);
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
  int fx = band < 10 && !tuning ? 190 : 222;
  label(s,fx,92,2);
  if (band < 10 && !tuning) button("MULTI",380,82,100,54);
  if (!range_matches()) label("Range changed: tap SWEEP",fx,123,1);
  else if (!ready) label((sweep_mode & SWEEP_ENABLE) ? "Measuring..." : "Paused",fx,123,1);
  else label(tuning ? "At selected frequency" : "Lowest sampled SWR",fx,123,1);
  label(ready ? cache.single.impedance : "Z --",190,140,2);
  if (tuning) {
    button("- FREQ",16,173,190,58); button("+ FREQ",274,173,190,58);
    uint32_t step = (antenna_bands[band].stop-antenna_bands[band].start)/200;
    if (step < 10) step = 10;
    plot_printf(s,sizeof(s),"Jog or tap: %u Hz step",step);
    label(s,24,248,1);
  } else {
    line(42,165,42,245); line(42,245,466,245);
    line(42,225,466,225); label("2:1",4,220,1); label("5",18,164,1);
    line(42,205,466,205); label("3:1",4,200,1);
    if (band == ANTENNA_60M_BAND) {
      for (unsigned i = 0; i < ANTENNA_60M_CHANNELS; ++i) {
        int x = 42 + (uint64_t)(antenna_60m_centers[i]-expected_start)*424 /
                     (expected_stop-expected_start);
        char channel[] = {(char)('1'+i), '\0'};
        label(channel,x-6,165,2);
        for (int y = 190; y < 245; y += 5) line(x,y,x,y+1);
      }
    }
    ink = GET_PALTETTE_COLOR(LCD_TRACE_1_COLOR);
    if (ready && count > 1)
      for (unsigned i = 1; i < count; ++i)
        if (curve[i-1] >= 1 && curve[i] >= 1)
          line(42+(i-1)*424/(count-1),graph_y(curve[i-1]),
                   42+i*424/(count-1),graph_y(curve[i]));
    ink = GET_PALTETTE_COLOR(LCD_FG_COLOR);
    plot_printf(s,sizeof(s),"%.4f",(double)expected_start/1000000.0); label(s,42,250,1);
    plot_printf(s,sizeof(s),"%.4f MHz",(double)expected_stop/1000000.0); label(s,363,250,1);
    // A separate rail below the axis leaves the SWR trace and channel guides
    // unobscured. These are the red CW/RTTY/data regions in the Extra row.
    const antenna_band_t *b = &antenna_bands[band];
    if (b->cw_data_stop) {
      int end = 42 + (uint64_t)(b->cw_data_stop-b->start)*424/(b->stop-b->start);
      ink = GET_PALTETTE_COLOR(LCD_TRACE_5_COLOR);
      for (int y = 264; y < 269; ++y) line(42,y,end,y);
      ink = GET_PALTETTE_COLOR(LCD_FG_COLOR);
    }
    if (ready && result.bandwidth_valid) {
      float width = (result.high_index-result.low_index) *
                    ((float)(expected_stop-expected_start)/(count-1))/1000;
      plot_printf(s,sizeof(s),"2:1 %s%.1f kHz%s",
          (result.left_limited || result.right_limited) ? ">=" : "",(double)width,
          (result.left_limited || result.right_limited) ? " (edge)" : "");
    } else strcpy(s,"2:1 width: --");
    label(s,12,147,1);
  }
  button("BANDS",0,276,120,44);
  bool running = (sweep_mode & SWEEP_ENABLE) != 0;
  pixel_t fg = ink, bg = paper;
  if (running && !tuning) {
    for (int y = 277; y < 319; ++y) line(121,y,238,y);
    ink = bg; paper = fg;
  }
  button("",120,276,120,44);
  label("SWEEP",128,281,2);
  label(tuning ? "FULL BAND" : (running ? "RUNNING" : "PAUSED"),128,306,1);
  ink = fg; paper = bg;
  button("TUNE",240,276,120,44); button("CAL",360,276,120,44);
}
bool antenna_ui_draw(void) {
  if (!active || ui_mode != UI_NORMAL) return false;
  /* A previous plot transfer may still own the shared buffer. Each lcd_bulk
   * below also waits for DMA before this buffer is reused for the next strip. */
  lcd_bulk_finish();
  paper = GET_PALTETTE_COLOR(LCD_BG_COLOR);
  for (strip_y = 0; strip_y < LCD_HEIGHT; strip_y += STRIP_HEIGHT) {
    ink = GET_PALTETTE_COLOR(LCD_FG_COLOR);
    for (unsigned i = 0; i < LCD_WIDTH*STRIP_HEIGHT; ++i) spi_buffer[i] = paper;
    draw_strip();
    int height = LCD_HEIGHT-strip_y;
    if (height > STRIP_HEIGHT) height = STRIP_HEIGHT;
    lcd_bulk(0,strip_y,LCD_WIDTH,height);
  }
  return true;
}
static void tune_step(bool up) {
  vna_band = band;
  const antenna_band_t *b = &antenna_bands[band];
  freq_t step = (b->stop-b->start)/200;
  if (step < 10) step = 10;
  if (up) tune_frequency = b->stop-tune_frequency < step ? b->stop : tune_frequency+step;
  else tune_frequency = tune_frequency-b->start < step ? b->start : tune_frequency-step;
  apply_range();
}
static void cable_vf_step(bool up) {
  if (up && velocity_factor < 100) ++velocity_factor;
  if (!up && velocity_factor > 1) --velocity_factor;
}
bool antenna_ui_touch(int x, int y) {
  if (!active) return false;
  touch_wait_release();
  if (cable) {
    if (y < 44 && x >= 380) { cable_leave(); apply_range(); save_mode(true); }
    else if (y >= 110 && y < 154) {
      if (x >= 338 && x < 398) cable_vf_step(false);
      else if (x >= 408 && x < 468) cable_vf_step(true);
    } else if (y >= 166 && y < 204 && x >= 12 && x < 322) {
      ui_mode_keypad(KM_ANT_CABLE_FREQ);
      return true;
    } else if (y >= 276) {
      if (x < 100) { cable_leave(); picker = true; page = 0; apply_range(); save_mode(true); }
      else if (x < 260) {
        cable_range = (cable_range+1)%5;
        if (cable_range == 1 && cable_target_band() == ANTENNA_BAND_COUNT) ++cable_range;
        apply_range();
      }
      else if (x < 380) {
        if (!range_matches()) apply_range();
        else sweep_mode ^= SWEEP_ENABLE;
        sweep_mode &= ~SWEEP_ONCE;
      } else if (cable_range < 2) {
        cal_mask = 1u << (cable_range == 1 && cable_target_band() >= 10);
        cable_leave(); cal_page = true; save_mode(true);
      } else {
        // Use the standard continuous-range CAL workflow and its ordinary slots.
        if (!range_matches()) apply_range();
        valid = false;
        menu_push_submenu(menu_cal_menu); ui_mode_menu();
        redraw(); draw_all(); menu_draw(-1);
        return true; // No later clear request may blank the freshly drawn menu.
      }
    }
    redraw(); return true;
  }
  if (cal_page) {
    unsigned stage=antenna_cal_stage();
    if (y>=276 && x<200) {
      antenna_cal_cancel(); cal_page=false; apply_range();
    } else if (y>=276) {
      if (!stage) {
        cal_mask |= required_profiles();
        if (cal_mask) antenna_cal_begin(cal_mask);
      } else if (!antenna_cal_step()) {
        ui_message_box("CAL ERROR","Capture/save failed. Retry or cancel.",1500);
      } else if (!antenna_cal_stage()) {
        cal_page=false; apply_range();
      }
    } else if (!stage) {
      if (y>=100 && y<175) cal_mask ^= 1;
      if (y>=182 && y<260) cal_mask ^= 2;
      cal_mask |= required_profiles();
    }
    redraw(); return true;
  }
  if (picker) {
    uint8_t list[ANTENNA_BAND_COUNT]; unsigned n = band_list(list);
    if (y < 36 && x < 170) multi_enter();
    else if (y < 36 && x < 300) { vna_band = band; cable_enter(); }
    else if (y < 36) {
      antenna_points = antenna_points == 101 ? 201 : (antenna_points == 201 ? 401 : 101);
      vna_band = band; tuning = false; apply_range();
    }
    else if (y >= 38 && y < 80) { group = x/160; page = 0; }
    else if (y >= 88 && y < 258) {
      unsigned j = page*6 + ((y-88)/58)*2 + x/240;
      if (j < n) { vna_band = band = list[j]; picker = false; tuning = false; apply_range(); }
    } else if (y >= 276) {
      if (x < 120 && page) --page;
      else if (x < 240 && x >= 120 && (page+1)*6 < n) ++page;
      else if (x >= 360) picker = false;
    }
  } else if (multi) {
    if (y<40 && x>=380) {
      band=multi_bands[multi_selected]; multi=multi_next=false;
      picker=true; page=0; apply_range();
    } else if (y<40 && x>=280) {
      if (!range_matches()) apply_range();
      else sweep_mode ^= SWEEP_ENABLE;
      sweep_mode &= ~SWEEP_ONCE;
    } else if (y<40 && x>=180) {
      leave();
    } else if (y>=40) multi_open((y-40)/140*4+x/120);
  } else if (y < 36 && x >= 380) leave();
  else if (y < 36 && x >= 335) {
    uint8_t old[3]; memcpy(old,config._reserved,3);
    uint16_t old_mode = config._vna_mode;
    favorites ^= 1u<<band;
    antenna_favorites_encode(config._reserved,favorites);
    config._vna_mode = (config._vna_mode & ~VNA_MODE_EU868_FAVORITE_MASK) |
      ((favorites & (1u<<ANTENNA_EU868_BAND)) ? VNA_MODE_EU868_FAVORITE_MASK : 0);
    if (config_save() != 0) {
      config._vna_mode = old_mode;
      memcpy(config._reserved,old,3); favorites = load_favorites();
      ui_message_box("Favorites","Save failed",1000);
    }
  } else if (y < 36 && x >= 250) {
    favorites_only = !favorites_only;
    group = favorites_only ? 2 : 0; page = 0;
  } else if (y >= 276) {
    if (x < 120) { picker = true; page = 0; }
    else if (x < 240) {
      vna_band = band;
      if (tuning || !range_matches()) { tuning = false; apply_range(); }
      else if (sweep_mode & SWEEP_ENABLE) sweep_mode &= ~(SWEEP_ENABLE | SWEEP_ONCE);
      else sweep_mode |= SWEEP_ENABLE;
    }
    else if (x < 360) {
      vna_band = band;
      if (!tuning) tune_frequency = valid && range_matches() ? get_frequency(result.minimum) :
                   antenna_bands[band].start+(antenna_bands[band].stop-antenna_bands[band].start)/2;
      tuning = true; apply_range();
    } else {
      vna_band = band;
      tuning=false; cal_page=true; cal_mask=3;
    }
  } else if (band < 10 && !tuning && x >= 380 && y >= 82 && y < 136) {
    multi_enter();
  } else if (tuning && y >= 173 && y < 231) {
    if (x < 206) tune_step(false); else if (x >= 274) tune_step(true);
  }
  if (active && !picker && !cal_page) save_mode(true);
  redraw(); return true;
}
bool antenna_ui_lever(uint16_t status) {
  if (!active) return false;
  if (cable) {
    if (status & (EVT_UP|EVT_DOWN)) cable_vf_step((status & EVT_UP) != 0);
    redraw(); return true;
  }
  if (cal_page) return true; // Walkthrough uses explicit touch actions.
  if (multi) {
    if (status & EVT_BUTTON_SINGLE_CLICK) multi_open(multi_selected);
    else if (status & (EVT_UP|EVT_DOWN))
      multi_selected=(multi_selected+((status & EVT_UP) ? 1 : 7))%8;
  } else if (status & EVT_BUTTON_SINGLE_CLICK) { picker = !picker; page = 0; }
  else if (tuning && !picker && (status & (EVT_UP|EVT_DOWN))) tune_step((status & EVT_UP) != 0);
  else if (status & (EVT_UP|EVT_DOWN)) {
    // Outside TUNE, rotation follows the ALL/FAV selection.
    unsigned next = band;
    for (unsigned i = 0; i < ANTENNA_BAND_COUNT; ++i) {
      next = (next + ((status & EVT_UP) ? 1 : ANTENNA_BAND_COUNT-1)) % ANTENNA_BAND_COUNT;
      if (!favorites_only || (favorites & (1u<<next))) {
        if (next != band || tuning || picker) {
          vna_band = band = next; picker = tuning = false; apply_range();
        }
        break;
      }
    }
  }
  if (!picker) save_mode(true);
  redraw(); return true;
}
#endif
