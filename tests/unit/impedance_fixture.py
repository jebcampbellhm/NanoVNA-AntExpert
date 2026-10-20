"""Link the production S11 helpers/callbacks into small host-only harnesses."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def functions(path, names):
    source = (ROOT / path).read_text()
    result = []
    for name in names:
        match = re.search(r'^(?:static inline )?float ' + name + r'\([^)]*\)\s*\{',
                          source, re.M)
        assert match, name
        end, depth = match.end(), 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        result.append(source[match.start():end])
    return '\n'.join(result)


def trace_math():
    return functions('include/ui/draw/plot_internal.h',
                     ('get_l', 'get_s11_r', 'get_s11_x')) + '\n' + functions(
        'src/ui/draw/traces.c', ('resistance', 'reactance', 'conductance', 'susceptance'))
