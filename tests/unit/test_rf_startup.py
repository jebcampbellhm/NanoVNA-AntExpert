"""Verify saved RF configuration precedes generator initialization."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[2]
text = (root / 'src/runtime/runtime_entry.c').read_text()
text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
order = [text.index(token) for token in (
    'platform_init();', 'state_manager_init();', 'drivers->generator->init();',
    'tlv320aic3204_init();', 'tlv320aic3204_start_clocks();', 'init_i2s(')]
assert order == sorted(order), 'saved RF configuration must precede generator init'
print('[PASS] saved RF configuration precedes generator and codec startup')
