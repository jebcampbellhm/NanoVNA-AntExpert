"""Keep firmware, calibration slots and device configuration disjoint."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
header = (ROOT / 'include/driver/stm32_peripherals.h').read_text()

def constants(name):
    return [int(x, 0) for x in re.findall(r'^#define ' + name + r'\s+(0x[0-9a-fA-F]+|\d+)', header, re.M)]

def region(text, name):
    origin, length = re.search(name + r'\s*:\s*org\s*=\s*(0x[0-9a-fA-F]+),\s*len\s*=\s*(\d+)k', text).groups()
    return int(origin, 16), int(length) * 1024

for index, (board, chip, total) in enumerate((('STM32F303', 'STM32F303xC', 256*1024),
                                            ('STM32F072', 'STM32F072xB', 128*1024))):
    script = (ROOT / 'boards' / board / (chip + '.ld')).read_text()
    code, code_size = region(script, 'flash0')
    storage, storage_size = region(script, 'flash7')
    assert code == 0x08000000
    assert code + code_size <= storage, f'{board}: code overlaps calibration flash'
    assert storage + storage_size <= code + total
    assert storage == constants('FLASH_CALIBRATION_SECTOR_START')[index]
    assert storage_size == constants('FLASH_CALIBRATION_SECTOR_SIZE')[index]
    required = constants('SAVE_CONFIG_SIZE')[index] + constants('SAVEAREA_MAX')[index] * constants('SAVE_PROP_CONFIG_SIZE')[index]
    assert required <= storage_size
    assert code + code_size <= storage + storage_size - required
print('[PASS] firmware/calibration/configuration flash regions do not overlap')

grid = (ROOT / 'include/processing/antenna_cal_grid.h').read_text()
scratch = int(re.search(r'ANT_CAL_SCRATCH (0x[0-9a-fA-F]+)', grid).group(1),16)
plane = int(re.search(r'ANT_CAL_PLANE_BYTES (\d+)', grid).group(1))
assert scratch >= constants('FLASH_CALIBRATION_SECTOR_START')[0] + constants('FLASH_CALIBRATION_SECTOR_SIZE')[0]
assert scratch % 2048 == 0 and plane % 2048 == 0
assert 401*2*4 <= plane and scratch+6*plane <= 0x08040000
print('[PASS] ANT staging is page-aligned and outside firmware/saved calibration/configuration')
