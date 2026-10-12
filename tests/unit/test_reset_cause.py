"""Check the production reset snapshot and its boot/reporting integration."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
runtime = (root / 'src/runtime/runtime_entry.c').read_text()
start = runtime.index('static void capture_reset_flags(void)')
capture = runtime[start:runtime.index('\n}', start) + 2]
boot = runtime[runtime.index('int runtime_main(void)'):]
assert boot.index('capture_reset_flags();') < boot.index('halInit();')
assert runtime.count('capture_reset_flags();') == 1
for path in ('src/sys/shell_commands.c', 'src/ui/menus/menu_system.c'):
    assert '"Reset flags: %08x"' in (root / path).read_text()

source = r'''
#include <stdint.h>
#include <assert.h>
static struct { volatile uint32_t CSR; } registers;
#define RCC (&registers)
#define RCC_CSR_RMVF (1u << 24)
static uint32_t runtime_reset_flags;
''' + capture + r'''
int main(void) {
  const uint32_t causes[] = {0, 1u<<25, 1u<<26, 1u<<27, 1u<<28,
                            1u<<29, 1u<<30, 1u<<31, 0xfc000003u};
  for (unsigned i=0; i<sizeof causes/sizeof causes[0]; ++i) {
    registers.CSR=causes[i];
    capture_reset_flags();
    assert(runtime_reset_flags==causes[i]);
    assert(registers.CSR==(causes[i]|RCC_CSR_RMVF));
    // Model hardware clearing sticky flags after RMVF: snapshot stays intact.
    registers.CSR=0;
    assert(runtime_reset_flags==causes[i]);
  }
  registers.CSR=1u<<28; capture_reset_flags();
  assert(runtime_reset_flags==(1u<<28));
  return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'reset.c'
    exe = Path(td) / 'reset'
    c.write_text(source)
    subprocess.run(shlex.split(os.environ.get('HOST_CC', 'cc')) +
                   ['-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Reset-cause tests passed')
