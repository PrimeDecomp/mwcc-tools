"""The text the kit's scripts print, from the methods' results: each function takes a method's result and gives its
lines (the oracle methods give theirs themselves, with `text=True`)."""
import json
import re

_OCCURS = re.compile(r'^(?:\S+?\.\w+: )?(old text occurs \d+ times \(must be exactly once\): )"(.*)"$', re.S)


def _edit_error(error):
    """An edit's error as try-variants printed it: without the file, the old text in Python's quoting."""
    match = _OCCURS.match(error or '')
    if not match:
        return error
    try:
        old = json.loads('"' + match.group(2) + '"')
    except ValueError:
        return error
    return match.group(1) + repr(old)


def try_variants(result):
    """source.variants' report: the unmodified source's match, then each variant's, with every other function whose
    match changed."""
    chosen = result.get('function')
    head = f"{result['unit']}: unmodified fuzzy {result['fuzzy']:.2f}%"
    if chosen:
        head += f", {chosen.get('demangled') or chosen['function']} {chosen['percent']:.2f}%"
    if result.get('identical_to_build') is False:
        head += ('  (WARNING: differs from the build object; run ninja, or the source has '
                 '__FILE__/path-dependent code)')
    lines = [head]
    for warning in result.get('warnings', []):
        lines.append(f'warning: {warning}')
    for variant in result['variants']:
        if variant['status'] != 'ok':
            lines.append(f"  {variant['name']}: {_edit_error(variant.get('error')) or variant['status']}")
            continue
        parts = [f"fuzzy {variant['fuzzy']:.2f}% ({variant['delta']:+.2f})"]
        if chosen:
            parts.insert(0, f"{variant['percent'] or 0:.2f}% ({variant['percent_delta'] or 0:+.2f})")
        lines.append(f"  {variant['name']}: " + ', '.join(parts))
        for change in sorted(variant.get('changed', []), key=lambda c: c['after'] - c['before']):
            regression = 'REGRESSION ' if change['after'] < change['before'] else ''
            lines.append(f"      {regression}{change['function']}: {change['before']:.2f}% -> {change['after']:.2f}%")
        if variant.get('unpaired'):
            lines.append(f"      no longer paired: {', '.join(sorted(variant['unpaired']))}")
    if result.get('applied'):
        written = ', '.join(result.get('applied_files') or [])
        lines.append(f"applied {result['applied']!r}" + (f' to {written}' if written else ''))
    return lines


def inline_sweep(result):
    """unit.inline_sweep's report: per unit and size, the configured and the trial match, and the functions that
    changed."""
    lines = []
    for unit in result['units']:
        name = unit['unit']
        if unit.get('error'):
            lines.append(f"{name}: {unit['error']}")
            continue
        if unit.get('source_sets'):
            lines.append(f'{name}: the source sets its own inline_max_size, so the flag measures nothing here')
        for size in unit['sizes']:
            if size['status'] != 'ok':
                console = (size.get('console') or '')[-300:]
                lines.append(f"{name}: {size['size']} compile failed: {console}")
                continue
            configured, trial = unit['configured_fuzzy'], size['fuzzy']
            mark = '+' if trial > configured + 1e-6 else '-' if trial < configured - 1e-6 else '='
            lines.append(f"{name:48s} {unit['configured']}: {configured:6.2f}   {size['size']}: {trial:6.2f}   {mark}")
            for change in sorted(size['changed'], key=lambda c: c['function']):
                show = lambda v: '-' if v is None else f'{v:.2f}'  # noqa: E731
                lines.append(f"     {show(change['before']):>6} -> {show(change['after']):>6}  {change['function'][:90]}")
    return lines


def residuals(result):
    """unit.residuals' report: each unit's nonmatching functions by verdict, then the count of each verdict."""
    lines = []
    current = None
    for row in sorted(result['rows'], key=lambda r: (r['unit'], -r['match'])):
        if row['unit'] != current:
            current = row['unit']
            lines.append(f'== {current}')
        name = row.get('demangled') or row['function']
        lines.append(f"   {row['verdict']:<14} {row['match']:6.2f}% {row['size']:6d}B  reg {row['register']:3d} "
                     f"stack {row['stack']:3d} other {row['other']:3d}  {name}")
    for failure in result.get('failed', []):
        lines.append(f"== {failure['unit']}: {failure['error']}")
    lines.append('')
    lines.append(', '.join(f'{k}: {v}' for k, v in sorted(result['summary'].items())))
    return lines


def sinit_profile(result):
    """unit.sinit_profile's report: one line per object."""
    return [f"{profile['name']} | {profile['line']}" for profile in result['profiles']]
