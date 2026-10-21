"""Write crates/mwcc-trace/src/m750.rs: the 750 machine model's timing table, from gc2/src/backend/MachineSimulation750.c.

    python tools/m750_table.py
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STAGES = ['BPU', 'IU1', 'IU2', 'LSU1', 'LSU2', 'FPU1', 'FPU2', 'FPU3', 'SRU']

source = (ROOT / 'gc2/src/backend/MachineSimulation750.c').read_text()
start = source.index('instruction_timing[OPCODE_MAX] = {')
body = source[start:source.index('};', start)]
rows = re.findall(r'^\s*(\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*//\s*(PC_\w+)', body, re.M)
names = [r[6] for r in rows]
lines = [
    '//! The PowerPC 750 machine model of GC 2.x\'s list scheduler (gc2/src/backend/MachineSimulation750.c), for',
    '//! [`crate::sched::SchedBlock::simulate`]: each opcode\'s first stage and its cycles in each stage. Written by',
    '//! tools/m750_table.py from gc2\'s table; the opcodes are GC 2.x\'s (PCodeInfo.h).',
    '',
    '/// The pipeline stages, in the model\'s order.',
    'pub(crate) const BPU: usize = 0;',
    'pub(crate) const IU1: usize = 1;',
    'pub(crate) const IU2: usize = 2;',
    'pub(crate) const LSU1: usize = 3;',
    'pub(crate) const LSU2: usize = 4;',
    'pub(crate) const FPU1: usize = 5;',
    'pub(crate) const FPU2: usize = 6;',
    'pub(crate) const FPU3: usize = 7;',
    'pub(crate) const SRU: usize = 8;',
    'pub(crate) const STAGES: usize = 9;',
    '',
    f'pub(crate) const PC_FDIV: i16 = {names.index("PC_FDIV")};',
    f'pub(crate) const PC_FDIVS: i16 = {names.index("PC_FDIVS")};',
    '',
    '/// Each opcode\'s first stage and its cycles in its first three stages.',
    '#[rustfmt::skip]',
    f'pub(crate) const TIMING: [(u8, [u8; 3]); {len(rows)}] = [',
]
for stage, _latency, c0, c1, c2, _serializes, name in rows:
    lines.append(f'    ({STAGES.index(stage)}, [{c0}, {c1}, {c2}]), // {name}')
lines.append('];')
(ROOT / 'crates/mwcc-trace/src/m750.rs').write_bytes(('\n'.join(lines) + '\n').encode())
print(f'{len(rows)} opcodes')
