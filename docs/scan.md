# A project's code: `project.scan`

```sh
mwcc-tools call project.scan project=PROJECT differing=true
mwcc-tools call project.scan project=PROJECT patterns=literal_reload units=Enemies/CDarkSamus
mwcc-tools call project.scan project=PROJECT function=Think__ instructions=true
```

`project.scan` reads every unit's target object and its built object (or the `units` named, by a part of their
names) and looks for patterns in each function's instructions, on both sides. For each pattern it gives the functions
of the target and of ours that have it, and those only one side has (`target_only`, `ours_only`). A pattern ours has
in many functions where the target has none is one cause, not many units' worth of misses: Echoes' whole scan takes
three seconds.

| Pattern | What |
|---|---|
| `literal_reload` | One symbol (a literal of the constant pool, a global) loaded more than once in a function. A function ours reloads a literal in that the target does not is a value numbering difference (a store or call between that kills it: see [value numbering](oracle/value-numbering.md)), or a literal ours made writable. |
| `repeated_load` | The same load, of one base register and offset or of one symbol, twice with no store, call or write of the base register between, in the order of the code (branches are not followed). |

`differing=true` keeps only the functions one side has a pattern in. `function` keeps the functions whose names have
it. `instructions=true` adds each function's instructions (`offset`, `text`, and the `symbol` its relocation names),
target and ours, for a scan of one's own. A relocated load's text has its base unresolved (`lfs f1, 0x0(r0)`): its
symbol says what it loads.

The objects are the build's as they are on disk: build the project first. Units with no target or compile edge are in
`skipped`.
