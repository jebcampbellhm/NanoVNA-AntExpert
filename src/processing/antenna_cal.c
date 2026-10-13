#include "nanovna.h"
#ifdef NANOVNA_F303
#include "processing/antenna_cal.h"
#include "sys/config_service.h"
#include "rf/sweep.h"
#include "driver/si5351.h"
#include "ui/core/ui_core.h"
#include <string.h>
#pragma GCC optimize("Os")

uint8_t antenna_cal_capture;
static uint8_t stage, selected, committed;
static bool modified;
static freq_t old_start, old_stop;
static uint16_t old_points, old_status;
static uint8_t old_sweep, power, band_mode;
static uint32_t threshold, xtal;
static int32_t offset;

bool antenna_cal_valid(const properties_t *p) {
  return p->magic == ANT_CAL_MAGIC && p->_reserved1[0] < 2 &&
    p->_cal_sweep_points == ANT_CAL_POINTS &&
    p->_cal_frequency0 == antenna_cal_frequency(p->_reserved1[0],0) &&
    p->_cal_frequency1 == antenna_cal_frequency(p->_reserved1[0],ANT_CAL_POINTS-1) &&
    (p->_cal_status & (CALSTAT_ED|CALSTAT_ES|CALSTAT_ER)) == (CALSTAT_ED|CALSTAT_ES|CALSTAT_ER);
}
static bool compatible(const properties_t *p) {
  return antenna_cal_valid(p) && p->_reserved1[1] == config._harmonic_freq_threshold &&
    p->_reserved1[2] == (uint32_t)config._IF_freq && p->_reserved1[3] == config._xtal_freq &&
    p->_reserved1[4] == config._band_mode;
}
bool antenna_cal_applied(unsigned profile) {
  return compatible(&current_props) && current_props._reserved1[0] == profile &&
    current_props._power == cal_power && (cal_status & CALSTAT_APPLY);
}
bool antenna_cal_load(unsigned profile) {
  if (stage || profile > 1) return false;
  const properties_t *p=get_properties(ANT_CAL_FIRST_SLOT+profile);
  if (!p || !compatible(p) || p->_reserved1[0] != profile) {
    if (current_props.magic == ANT_CAL_MAGIC) cal_status &= ~CALSTAT_APPLY;
    return false;
  }
  memcpy(cal_data,p->_cal_data,sizeof cal_data);
  memcpy(current_props._reserved1,p->_reserved1,sizeof current_props._reserved1);
  current_props.magic=ANT_CAL_MAGIC;
  cal_frequency0=p->_cal_frequency0; cal_frequency1=p->_cal_frequency1;
  cal_sweep_points=ANT_CAL_POINTS; cal_power=p->_cal_power;
  current_props._power=cal_power;
  cal_status=CALSTAT_ED|CALSTAT_ES|CALSTAT_ER|CALSTAT_APPLY|CALSTAT_INTERPOLATED;
  lastsaveid=ANT_CAL_FIRST_SLOT+profile;
  return true;
}
bool antenna_cal_saved(unsigned profile) {
  if (profile > 1) return false;
  const properties_t *p=get_properties(ANT_CAL_FIRST_SLOT+profile);
  return p && compatible(p) && p->_reserved1[0]==profile;
}
bool antenna_cal_interpolate(freq_t f, float out[CAL_TYPE_COUNT][2]) {
  if (current_props.magic != ANT_CAL_MAGIC) return false;
  // Identity outside covered bands, or after incompatible RF setting changes.
  memset(out,0,CAL_TYPE_COUNT*2*sizeof(float));
  out[ETERM_ER][0]=out[ETERM_ET][0]=1;
  unsigned i; float k;
  unsigned p=current_props._reserved1[0];
  if (!compatible(&current_props) || current_props._power != cal_power ||
      !antenna_cal_bracket(p,f,&i,&k)) return true;
  freq_t a=antenna_cal_frequency(p,i), b=antenna_cal_frequency(p,i+1);
  // Never mix coefficients from different harmonic modes.
  if (si5351_get_harmonic_lvl(a) != si5351_get_harmonic_lvl(b))
    k=si5351_get_harmonic_lvl(f)==si5351_get_harmonic_lvl(a) ? 0 : 1;
  for (unsigned t=0;t<3;++t)
    for (unsigned c=0;c<2;++c)
      out[t][c]=cal_data[t][i][c]+k*(cal_data[t][i+1][c]-cal_data[t][i][c]);
  return true;
}
unsigned antenna_cal_stage(void) { return stage; }
static const void *raw(unsigned step, unsigned profile) {
  return (const void *)(uintptr_t)(ANT_CAL_SCRATCH+(step*2+profile)*ANT_CAL_PLANE_BYTES);
}
static bool finite_samples(const float *v, unsigned count) {
  for (unsigned i=0;i<count;++i) {
    uint32_t bits; memcpy(&bits,v+i,4);
    if ((bits & 0x7f800000u)==0x7f800000u) return false;
  }
  return true;
}
bool antenna_cal_begin(unsigned mask) {
  if (stage || calibration_in_progress || !mask || mask>3) return false;
  for (unsigned p=0;p<2;++p) {
    if (!antenna_cal_saved(p)) mask|=1u<<p;
  }
  old_start=frequency0; old_stop=frequency1; old_points=sweep_points;
  old_status=cal_status; old_sweep=sweep_mode;
  power=current_props._power; band_mode=config._band_mode;
  threshold=config._harmonic_freq_threshold; offset=config._IF_freq; xtal=config._xtal_freq;
  selected=mask; committed=0; modified=false; stage=1; calibration_in_progress=true;
  sweep_mode &= ~(SWEEP_ENABLE|SWEEP_ONCE);
  return true;
}
void antenna_cal_cancel(void) {
  if (!stage) return;
  antenna_cal_capture=0; stage=0; calibration_in_progress=false;
  frequency0=old_start; frequency1=old_stop; sweep_points=old_points;
  sweep_mode=old_sweep;
  cal_status=modified ? 0 : old_status;
  app_measurement_update_frequencies();
}
bool antenna_cal_step(void) {
  if (!stage || current_props._power!=power || config._band_mode!=band_mode ||
      config._harmonic_freq_threshold!=threshold || config._IF_freq!=offset || config._xtal_freq!=xtal)
    return false;
  if (stage<4) {
    for (unsigned p=0;p<2;++p) if (selected & (1u<<p)) {
      ui_message_box_draw(stage==1 ? "OPEN" : stage==2 ? "SHORT" : "LOAD",
                          p ? "Capturing V/U ANT - 401 points" : "Capturing HF ANT - 401 points");
      sweep_points=ANT_CAL_POINTS;
      antenna_cal_capture=p+1;
      uint16_t bw=config._bandwidth;
      if (bw<BANDWIDTH_100) config._bandwidth=BANDWIDTH_100;
      bool ok=app_measurement_sweep(false,SWEEP_CH0_MEASURE);
      config._bandwidth=bw; antenna_cal_capture=0;
      if (!ok || !finite_samples(&measured[0][0][0],ANT_CAL_POINTS*2) ||
          antenna_cal_stage_write((stage-1)*2+p,measured[0],sizeof measured[0])) return false;
    }
    ++stage;
    return true;
  }
  // Check all staged standards before modifying either destination.
  for (unsigned p=0;p<2;++p) if (selected & (1u<<p)) {
    const float (*o)[2]=raw(0,p), (*s)[2]=raw(1,p);
    for (unsigned i=0;i<ANT_CAL_POINTS;++i) {
      float re=o[i][0]-s[i][0], im=o[i][1]-s[i][1];
      if (re*re+im*im < 1e-12f) return false;
    }
  }
  for (unsigned p=0;p<2;++p) if ((selected & ~committed) & (1u<<p)) {
    modified=true;
    sweep_points=ANT_CAL_POINTS;
    memcpy(cal_data[CAL_OPEN],raw(0,p),sizeof cal_data[0]);
    memcpy(cal_data[CAL_SHORT],raw(1,p),sizeof cal_data[0]);
    memcpy(cal_data[CAL_LOAD],raw(2,p),sizeof cal_data[0]);
    cal_status=CALSTAT_OPEN|CALSTAT_SHORT|CALSTAT_LOAD;
    cal_done(); calibration_in_progress=true;
    if (!finite_samples(&cal_data[0][0][0],CAL_TYPE_COUNT*ANT_CAL_POINTS*2)) return false;
    current_props.magic=ANT_CAL_MAGIC;
    cal_frequency0=antenna_cal_frequency(p,0);
    cal_frequency1=antenna_cal_frequency(p,ANT_CAL_POINTS-1);
    cal_sweep_points=ANT_CAL_POINTS; cal_power=power;
    current_props._reserved1[0]=p;
    current_props._reserved1[1]=threshold; current_props._reserved1[2]=(uint32_t)offset;
    current_props._reserved1[3]=xtal; current_props._reserved1[4]=band_mode;
    // Save a conventional display range; grid metadata applies only to calibration.
    frequency0=cal_frequency0; frequency1=cal_frequency1;
    if (antenna_cal_store(p)) return false;
    committed|=1u<<p;
  }
  antenna_cal_cancel();
  return true;
}
#endif
