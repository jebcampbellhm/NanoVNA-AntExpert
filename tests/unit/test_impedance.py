"""Check real VNA callbacks against known complex loads, also with fast math."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
from impedance_fixture import trace_math

SOURCE = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static float port_z;
#define PORT_Z port_z
''' + trace_math() + r'''
static void near(float actual, float expected) {
  assert(fabsf(actual-expected) <= 0.0002f*fmaxf(1, fabsf(expected)));
}
static void load(float r, float x) {
  // Gamma=(Z-Z0)/(Z+Z0), independent of the callback's transformed inputs.
  float d=(r+port_z)*(r+port_z)+x*x;
  float v[]={(r*r+x*x-port_z*port_z)/d, 2*port_z*x/d};
  near(resistance(0,v),r); near(reactance(0,v),x);
  if (r*r+x*x > 0) {
    near(conductance(0,v),r/(r*r+x*x));
    near(susceptance(0,v),-x/(r*r+x*x));
  }
}
int main(void) {
  for (unsigned p=0;p<2;p++) {
    port_z=p?75:50;
    const float matched[]={0,0}, shorted[]={-1,0};
    near(resistance(0,matched),port_z); near(reactance(0,matched),0);
    near(conductance(0,matched),1/port_z); near(susceptance(0,matched),0);
    near(resistance(0,shorted),0); near(reactance(0,shorted),0);
    load(25,0); load(50,0); load(75,0); load(100,0); load(1000,0);
    load(50,25); load(50,-25); load(0,50); load(0,-50);
    load(12.5f,200); load(12.5f,-200); load(500,500); load(500,-500);
    load(-5,25); // Noisy/active inputs must not silently turn into positive R.
    volatile float open_real=1; // Keep the singular case a runtime RF input.
    const float opened[]={open_real,0};
    near(conductance(0,opened),0); near(susceptance(0,opened),0);
#ifndef __FAST_MATH__
    // An ideal open is singular for Z, not a spurious 50-ohm match.
    // Fast math has no contract for 0/0; ANT guards this before conversion.
    float r=resistance(0,opened); uint32_t bits; memcpy(&bits,&r,4);
    assert((bits&0x7f800000u)==0x7f800000u);
#endif
  }
  puts("S11 impedance/admittance: matched, short/open, resistive, inductive, capacitive and 75-ohm loads passed");
}
'''
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d/'test.c').write_text(SOURCE)
    for flags in ([], ['-ffast-math']):
        subprocess.run(shlex.split(os.environ.get('HOST_CC', 'cc')) + [
            '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', *flags,
            str(d/'test.c'), '-lm', '-o', str(d/'test')], check=True)
        subprocess.run([str(d/'test')], check=True)
