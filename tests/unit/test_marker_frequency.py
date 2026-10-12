"""Compile the production marker remapping function, including CW spans."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
text = (root / 'src/runtime/runtime_entry.c').read_text()
start = text.index('static void update_marker_index(freq_t start, freq_t stop, uint16_t points) {')
opening = text.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
typedef uint32_t freq_t;
#define MARKERS_MAX 4
static struct { bool enabled; uint32_t frequency; unsigned index; } markers[4];
static void set_marker_index(unsigned i,unsigned idx) { markers[i].index=idx; }
''' + text[start:end] + r'''
int main(void) {
  markers[0].enabled=true;
  markers[0].frequency=14000000;
  update_marker_index(14000000,14000000,21); assert(markers[0].index==0);
  markers[0].frequency=15000000;
  update_marker_index(14000000,14000000,401); assert(markers[0].index==0);
  markers[0].frequency=14000001;
  update_marker_index(14000000,14000002,401); assert(markers[0].index==200);
  markers[0].frequency=1000000;
  update_marker_index(14000000,14350000,101); assert(markers[0].index==0);
  markers[0].frequency=30000000;
  update_marker_index(14000000,14350000,101); assert(markers[0].index==100);
  markers[0].frequency=14175000;
  update_marker_index(14000000,14350000,101); assert(markers[0].index==50);
  markers[0].frequency=1350000000;
  update_marker_index(0,2700000000u,401); assert(markers[0].index==200);
  update_marker_index(0,2700000000u,1); assert(markers[0].index==0);
  markers[0].index=17;
  update_marker_index(0,2700000000u,0); assert(markers[0].index==17);
  markers[0].enabled=false;
  update_marker_index(0,2700000000u,401); assert(markers[0].index==17);
  puts("marker mapping: CW, tiny spans, bounds and wide spans passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p / 'test.c').write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC', 'cc')) + [
        '-std=c11', '-Wall', '-Wextra', '-Werror', str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
