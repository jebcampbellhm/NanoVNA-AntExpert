/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "nanovna.h"

/*
 * Regression suite for the math-only helpers in src/rf/analysis/legacy_measure.c.
 *
 * The legacy RF analytics module performs all of its cursor searches and
 * regressions on host-side float buffers before any UI gets involved.  Because
 * these helpers are self-contained (no hardware dependencies), we can run
 * high-confidence tests on every GitHub build and immediately detect when a
 * refactor breaks the interpolation math, marker bookkeeping, or polynomial
 * fits.  Each test below feeds synthetic sweep data into the exact routines the
 * firmware uses and asserts the resulting frequencies/coefficients.
 */

uint16_t sweep_points;
freq_t frequency0;
freq_t frequency1;
float measure_frequency_step;
alignas(8) float measured[2][SWEEP_POINTS_MAX][2];
config_t config;
properties_t current_props;

static float g_curve_data[SWEEP_POINTS_MAX];
static float g_regression_x[32];
static float g_regression_y[32];
static int g_failures = 0;
static int g_last_marker_slot = -1;
static int g_last_marker_index = -1;

#define CHECK(cond)                                                                           \
  do {                                                                                        \
    if (!(cond)) {                                                                            \
      ++g_failures;                                                                           \
      fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
    }                                                                                         \
  } while (0)

static void expect_float_close(float expected, float actual, float tol, const char* label) {
  if (fabsf(expected - actual) > tol) {
    ++g_failures;
    fprintf(stderr, "[FAIL] %s expected=%f actual=%f tol=%f\n", label, expected, actual, tol);
  }
}

static void configure_sweep(uint16_t points, freq_t start, float step_hz) {
  sweep_points = points;
  frequency0 = start;
  measure_frequency_step = step_hz;
  frequency1 = start + (freq_t)((points > 0 ? (points - 1) : 0) * step_hz);
}

static void reset_marker_log(void) {
  g_last_marker_slot = -1;
  g_last_marker_index = -1;
}

void set_marker_index(int marker, int idx) {
  g_last_marker_slot = marker;
  g_last_marker_index = idx;
}

freq_t get_frequency(uint16_t idx) {
  return frequency0 + (freq_t)(measure_frequency_step * idx);
}

freq_t get_frequency_step(void) {
  return (freq_t)measure_frequency_step;
}

freq_t get_sweep_frequency(uint16_t idx) {
  return get_frequency(idx);
}

static float curve_value(uint16_t idx) {
  return g_curve_data[idx];
}

static float regression_get_x(uint16_t idx) {
  return g_regression_x[idx];
}

static float regression_get_y(uint16_t idx) {
  return g_regression_y[idx];
}



// Mocks for legacy_measure dependencies
float resistance(int i, const float* v) { (void)i; (void)v; return 50.0f; }
float reactance(int i, const float* v) { (void)i; (void)v; return 0.0f; }
float swr(int i, const float* v) { (void)i; (void)v; return 1.0f; }
float logmag(int i, const float* v) { (void)i; (void)v; return 0.0f; }
void invalidate_rect(int x, int y, int w, int h) {
  (void)x; (void)y; (void)w; (void)h;
}
void cell_printf(int x, int y, const char* fmt, ...) {
  (void)x; (void)y; (void)fmt;
}
void markmap_all_markers(void) {}

#include "rf/analysis.h"
// The firmware includes these static view/controller routines inside plot.c.
// Include them here too so their actual size-optimized paths get exercised.
#include "../../src/rf/legacy.c"

static void test_match_quadratic_equation(void) {
  /*
   * The quadratic solver underpins both marker searches and the LC matching
   * math.  Two scenarios are covered: a classic polynomial with real roots and
   * another with a negative discriminant that should clamp to zeros.
   */
  float roots[2];
  match_quadratic_equation(1.0f, -5.0f, 6.0f, roots); /* (x-2)(x-3)=0 */
  expect_float_close(3.0f, roots[0], 1e-6f, "quadratic root[0]");
  expect_float_close(2.0f, roots[1], 1e-6f, "quadratic root[1]");

  match_quadratic_equation(1.0f, 0.0f, 4.0f, roots); /* no real roots */
  expect_float_close(0.0f, roots[0], 1e-6f, "quadratic no-root[0]");
  expect_float_close(0.0f, roots[1], 1e-6f, "quadratic no-root[1]");
}

static void build_symmetric_parabola(float center) {
  for (uint16_t i = 0; i < sweep_points; ++i) {
    float delta = (float)i - center;
    g_curve_data[i] = delta * delta;
  }
}

static void test_measure_search_value_right(void) {
  /*
   * Searching from the left of a crossing should produce the expected marker
   * book-keeping, and the returned frequency must be interpolated between
   * samples.  The parabola y=(x-5)^2 crosses y=4 at x=3,7; here we start left
   * of the first crossing and scan right.
   */
  configure_sweep(16, 1000000U, 1000.0f);
  build_symmetric_parabola(5.0f);
  reset_marker_log();

  uint16_t idx = 2;
  float freq = measure_search_value(&idx, 4.0f, curve_value, MEASURE_SEARCH_RIGHT, 5);
  const float expected = (float)frequency0 + measure_frequency_step * 3.0f;
  expect_float_close(expected, freq, 0.5f, "search right frequency");
  CHECK(idx == 2); /* marker sticks to the last >y sample */
  CHECK(g_last_marker_slot == 5);
  CHECK(g_last_marker_index == 2);
}

static void test_measure_search_value_left(void) {
  /*
   * Mirrored search starting to the right of the same crossing.  The helper
   * should step backwards, update the caller's index to the last <= y element,
   * and emit a negative fractional offset so the frequency remains correct.
   */
  configure_sweep(16, 2000000U, 500.0f);
  build_symmetric_parabola(5.0f);
  reset_marker_log();

  uint16_t idx = 8;
  float freq = measure_search_value(&idx, 4.0f, curve_value, MEASURE_SEARCH_LEFT, 6);
  const float expected = (float)frequency0 + measure_frequency_step * 7.0f;
  expect_float_close(expected, freq, 0.5f, "search left frequency");
  CHECK(idx == 8);
  CHECK(g_last_marker_slot == 6);
  CHECK(g_last_marker_index == 8);
}

static void test_search_peak_value_max(void) {
  /*
   * search_peak_value() scans the sweep for a global extremum and refines it
   * with a parabolic interpolation.  A downward-opening parabola guarantees the
   * function locates a clean maximum away from the edges.
   */
  configure_sweep(9, 0U, 1.0f);
  for (uint16_t i = 0; i < sweep_points; ++i) {
    float delta = (float)i - 4.0f;
    g_curve_data[i] = 10.0f - delta * delta;
  }
  uint16_t peak_idx = 0;
  float peak = search_peak_value(&peak_idx, curve_value, MEASURE_SEARCH_MAX);
  expect_float_close(10.0f, peak, 1e-5f, "search peak max value");
  CHECK(peak_idx == 4);
}

static void test_search_peak_value_min(void) {
  /*
   * The same routine with MEASURE_SEARCH_MIN must return the minimum of an
   * upward-opening parabola and provide the correct cursor index.
   */
  configure_sweep(11, 0U, 1.0f);
  for (uint16_t i = 0; i < sweep_points; ++i) {
    float delta = (float)i - 5.0f;
    g_curve_data[i] = delta * delta;
  }
  uint16_t min_idx = 0;
  float trough = search_peak_value(&min_idx, curve_value, MEASURE_SEARCH_MIN);
  expect_float_close(0.0f, trough, 1e-6f, "search peak min value");
  CHECK(min_idx == 5);
}

static void test_parabolic_regression(void) {
  /*
   * parabolic_regression() solves a 3x3 normal equation for a polynomial fit.
   * Feed it synthetic y = 1 + 2x + 0.5x^2 samples and verify that the fitted
   * coefficients match the ground truth within a tiny epsilon.
   */
  const int samples = 6;
  for (int i = 0; i < samples; ++i) {
    float x = (float)i;
    g_regression_x[i] = x;
    g_regression_y[i] = 1.0f + 2.0f * x + 0.5f * x * x;
  }
  float coeff[3] = {0};
  parabolic_regression(samples, regression_get_x, regression_get_y, coeff);
  expect_float_close(1.0f, coeff[0], 1e-5f, "regression coeff a");
  expect_float_close(2.0f, coeff[1], 1e-5f, "regression coeff b");
  expect_float_close(0.5f, coeff[2], 1e-5f, "regression coeff c");
}

static void open_cable(float metres, float vf, float reflection) {
  for (unsigned i=0; i<sweep_points; ++i) {
    // Round trip phase of an open-ended uniform line. VF is a fraction here.
    float phase = 4.0f * 3.14159265358979323846f * get_frequency(i) * metres /
                  (299792458.0f * vf);
    measured[0][i][0] = reflection * cosf(phase);
    measured[0][i][1] = -reflection * sinf(phase);
  }
}
static void test_cable_phase(void) {
  const float lengths[] = {0.25f,1,3.48f,10,30.48f,100,500,1000};
  const float factors[] = {0.66f,0.85f,1};
  configure_sweep(SWEEP_POINTS_MAX,28000000,1700000.0f/(SWEEP_POINTS_MAX-1));
  for(unsigned l=0;l<sizeof lengths/sizeof *lengths;++l)
    for(unsigned v=0;v<sizeof factors/sizeof *factors;++v) {
      open_cable(lengths[l],factors[v],0.8f);
      float delay=cable_phase_delay(); CHECK(delay>0);
      expect_float_close(lengths[l],delay*299792458.0f*factors[v]/2,
                         lengths[l]*0.003f,"HF phase cable length");
    }
  // Realistic small phase ripple does not upset the delay fit or phase wraps.
  open_cable(30.48f,0.66f,0.8f);
  for(unsigned i=0;i<sweep_points;i++) {
    float theta=0.005f*sinf(i*0.7f), re=measured[0][i][0], im=measured[0][i][1];
    measured[0][i][0]=re*cosf(theta)-im*sinf(theta);
    measured[0][i][1]=re*sinf(theta)+im*cosf(theta);
  }
  expect_float_close(30.48f,cable_phase_delay()*299792458.0f*0.66f/2,0.10f,"HF phase with ripple");
  // Quarter-wave refinement can work on the narrow 30 m band and on 2 m.
  configure_sweep(SWEEP_POINTS_MAX,10100000,50000.0f/(SWEEP_POINTS_MAX-1));
  open_cable(299792458.0f*0.66f/(4*10125000),0.66f,1);
  expect_float_close(1.0f/(2*10125000),cable_phase_delay(),1.5e-10f,"30 m quarter-wave delay");
  configure_sweep(SWEEP_POINTS_MAX,144000000,4000000.0f/(SWEEP_POINTS_MAX-1));
  open_cable(299792458.0f*0.66f/(4*145000000),0.66f,1);
  expect_float_close(1.0f/(2*145000000),cable_phase_delay(),1e-11f,"2 m quarter-wave delay");
  configure_sweep(SWEEP_POINTS_MAX,28000000,1700000.0f/(SWEEP_POINTS_MAX-1));
  open_cable(0,0.66f,1); CHECK(cable_phase_delay()==0);
  open_cable(-10,0.66f,1); CHECK(cable_phase_delay()==0);
  open_cable(10,0.66f,0); CHECK(cable_phase_delay()==0);
  open_cable(10,0.66f,0.1f); CHECK(cable_phase_delay()==0);
  const uint32_t bad[]={0x7fc00000,0x7f800000,0xff800000,0x7f7fffff};
  for(unsigned b=0;b<sizeof bad/sizeof *bad;++b) for(unsigned c=0;c<2;c++) {
    open_cable(10,0.66f,1); memcpy(&measured[0][17][c],&bad[b],4);
    CHECK(cable_phase_delay()==0);
  }
  // Curved/oscillating phase is not a simple cable; nearly aliased steps fail.
  for(unsigned i=0;i<sweep_points;i++) {
    float theta=2.0f*i/(sweep_points-1)+0.8f*sinf(i*0.15f);
    measured[0][i][0]=cosf(theta); measured[0][i][1]=-sinf(theta);
  }
  CHECK(cable_phase_delay()==0);
  configure_sweep(5,28000000,425000);
  open_cable(110,0.66f,1); CHECK(cable_phase_delay()==0);
  sweep_points=4; CHECK(cable_phase_delay()==0);
  sweep_points=SWEEP_POINTS_MAX+1; CHECK(cable_phase_delay()==0);
  sweep_points=SWEEP_POINTS_MAX; frequency1=frequency0; CHECK(cable_phase_delay()==0);
  frequency1=frequency0-1; CHECK(cable_phase_delay()==0);
}

static void test_cable_length(void) {
  configure_sweep(SWEEP_POINTS_MAX,50000,(50000000.0f-50000)/(SWEEP_POINTS_MAX-1));
  const float lengths[] = {1.5f,3.0f,10.0f,20.0f};
  const float factors[] = {0.66f,0.80f,0.85f};
  for (unsigned l=0;l<sizeof lengths/sizeof *lengths;++l)
    for (unsigned v=0;v<sizeof factors/sizeof *factors;++v) {
      open_cable(lengths[l],factors[v],0.8f);
      float f=cable_quarter_wave_frequency();
      CHECK(f>0);
      if(f>0) expect_float_close(lengths[l],299792458.0f*factors[v]/(4*f),
                                 lengths[l]*0.015f,"open cable length");
    }
  // Standard VNA cable measurement and known-length/VF calculation are kept.
  open_cable(10,0.66f,0.8f); velocity_factor=66; real_cable_len=0;
  prepare_s11_cable(0,MEASURE_UPD_SWEEP);
  expect_float_close(10,measure_cache.s11_cable.len,0.15f,"standard VNA cable length");
  real_cable_len=10; prepare_s11_cable(0,MEASURE_UPD_SWEEP);
  expect_float_close(66,measure_cache.s11_cable.vf,1,"standard VNA velocity factor");
  real_cable_len=0;
  // A short jumper has no quarter-wave crossing in the 50 MHz range.
  open_cable(0.2f,0.66f,1); CHECK(cable_quarter_wave_frequency()==0);
  configure_sweep(SWEEP_POINTS_MAX,50000,(300000000.0f-50000)/(SWEEP_POINTS_MAX-1));
  open_cable(0.2f,0.66f,1); CHECK(cable_quarter_wave_frequency()>0);
  configure_sweep(SWEEP_POINTS_MAX,50000,(5000000.0f-50000)/(SWEEP_POINTS_MAX-1));
  open_cable(100,0.66f,0.8f);
  float f=cable_quarter_wave_frequency(); CHECK(f>0);
  if(f>0) expect_float_close(100,299792458.0f*0.66f/(4*f),0.5f,"long cable");
  // A matched load, excessive attenuation, a bare OPEN and a SHORT-ended line.
  open_cable(10,0.66f,0); CHECK(cable_quarter_wave_frequency()==0);
  open_cable(10,0.66f,0.1f); CHECK(cable_quarter_wave_frequency()==0);
  open_cable(0,0.66f,1); CHECK(cable_quarter_wave_frequency()==0);
  open_cable(100,0.66f,-1); CHECK(cable_quarter_wave_frequency()==0);
  // Sparse first lobe and a sweep beginning after the first quarter wave.
  configure_sweep(5,50000,10000000);
  open_cable(10,0.66f,1); CHECK(cable_quarter_wave_frequency()==0);
  configure_sweep(SWEEP_POINTS_MAX,10000000,10000);
  open_cable(3,0.66f,1); CHECK(cable_quarter_wave_frequency()==0);
  configure_sweep(SWEEP_POINTS_MAX,50000,50000);
  const uint32_t bad[] = {0x7fc00000u,0x7f800000u,0xff800000u};
  for(unsigned b=0;b<3;++b) for(unsigned ch=0;ch<2;++ch) {
    open_cable(30,0.66f,1); memcpy(&measured[0][2][ch],&bad[b],4);
    CHECK(cable_quarter_wave_frequency()==0);
  }
  open_cable(30,0.66f,1);
  measured[0][0][1]=0.001f; // Low-frequency noise must not become a huge length.
  CHECK(cable_quarter_wave_frequency()>0);
  frequency1=frequency0; CHECK(cable_quarter_wave_frequency()==0);
  frequency1=frequency0-1; CHECK(cable_quarter_wave_frequency()==0);
  configure_sweep(4,50000,100000); CHECK(cable_quarter_wave_frequency()==0);
  sweep_points=SWEEP_POINTS_MAX+1; CHECK(cable_quarter_wave_frequency()==0);
  configure_sweep(SWEEP_POINTS_MAX,0,50000); CHECK(cable_quarter_wave_frequency()==0);
  configure_sweep(9,50000,50000);
  for(unsigned i=0;i<9;i++) {
    measured[0][i][0]=i<3?1:-1;
    const float im[]={-0.05f,-0.3f,-0.6f,-0.8f,-0.6f,-0.3f,0,0.3f,0.6f};
    measured[0][i][1]=im[i];
  }
  CHECK(cable_quarter_wave_frequency()==350000); // Exact-zero sample is valid.
  measured[0][2][0]=1e30f; CHECK(cable_quarter_wave_frequency()==0);
}

int main(void) {
  memset(&config, 0, sizeof(config));
  config._measure_r = 50.0f;
  test_match_quadratic_equation();
  test_measure_search_value_right();
  test_measure_search_value_left();
  test_search_peak_value_max();
  test_search_peak_value_min();
  test_parabolic_regression();
  test_cable_length();
  test_cable_phase();

  if (g_failures == 0) {
    puts("[PASS] tests/unit/test_legacy_measure");
    return EXIT_SUCCESS;
  }
  fprintf(stderr, "[FAIL] %d test(s) failed\n", g_failures);
  return EXIT_FAILURE;
}
