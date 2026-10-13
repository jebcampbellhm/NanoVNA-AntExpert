"""Compare every production H4 decoded glyph byte with the original font."""
from pathlib import Path
import os
import runpy
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
generator = runpy.run_path(str(ROOT/'tools/pack_numfont.py'))
source = ROOT/'src/ui/resources/fonts/numfont16x22.c'
expected = generator['generate'](source.read_text(encoding='utf-8'))
assert source.with_name('numfont16x22_compact.h').read_text(encoding='utf-8') == expected
HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern const uint8_t numfont16x22[];
void numfont16x22_decode(uint8_t,uint8_t[44]);
int main(void) {
  uint8_t guarded[46], other[44];
  for(unsigned ch=0;ch<23;ch++) {
    memset(guarded,0xa5,sizeof guarded);
    numfont16x22_decode(ch,guarded+1);
    assert(guarded[0]==0xa5 && guarded[45]==0xa5);
    assert(!memcmp(guarded+1,numfont16x22+44*ch,44));
    numfont16x22_decode((ch+1)%23,other);
    assert(!memcmp(guarded+1,numfont16x22+44*ch,44)); // No shared glyph buffer.
  }
  for(unsigned ch=23;ch<256;ch++) {
    numfont16x22_decode(ch,guarded+1);
    for(unsigned i=1;i<=44;i++) assert(!guarded[i]);
    assert(guarded[0]==0xa5 && guarded[45]==0xa5);
  }
  puts("numeric font: all 23 glyphs identical; generation, bounds and independent buffers passed");
}
'''
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d/'nanovna.h').write_text('#define _BMP16(v) (((v)>>8)&255),((v)&255)\n', encoding='utf-8')
    (d/'test.c').write_text(HARNESS, encoding='utf-8')
    command = shlex.split(os.environ.get('HOST_CC','cc'))
    flags = ['-std=c11','-Wall','-Wextra','-Werror','-I'+str(d)]
    subprocess.run(command+flags+['-c',str(source),'-o',str(d/'raw.o')],check=True)
    subprocess.run(command+flags+['-DNANOVNA_F303','-c',str(source),'-o',str(d/'packed.o')],check=True)
    subprocess.run(command+flags+[str(d/'test.c'),str(d/'raw.o'),str(d/'packed.o'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
