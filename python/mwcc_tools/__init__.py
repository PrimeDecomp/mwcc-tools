"""The mwcc engine from Python: a client of `mwcc-tools serve` (standard library only).

    from mwcc_tools import Engine, Unit
    with Engine(project='path/to/echoes') as engine:
        unit = Unit(engine, 'main/MetroidPrime/CControlMapper')
        print(unit.score()['score']['fuzzy'])
"""
from . import reports
from .engine import SCHEMA_VERSION, Engine, EngineError, Trace, Unit, find_binary
from .experiments import (checked_replace, matrix_cases, refresh_coverage, run_matrix, run_variants,
                          score_symbols)

__all__ = ['SCHEMA_VERSION', 'Engine', 'EngineError', 'Trace', 'Unit', 'checked_replace', 'find_binary',
           'matrix_cases', 'refresh_coverage', 'reports', 'run_matrix', 'run_variants', 'score_symbols']
