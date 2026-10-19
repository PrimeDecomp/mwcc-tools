These standalone inputs exercise the bounded source-node observation contract.
They need no game files or headers.

`ordinary_writes.cpp` contains three ordinary assignment statements.
`inline_arrays.cpp` applies a small helper to two distinct arrays and nests
another inline helper in its arguments. `unsupported_call.cpp` contains an
indirect call statement whose tree must remain explicitly opaque.

Fixture syntax is an experiment, not proof that a compiler version takes a
particular inline route. The verifier retains actual emitted scopes, snapshots
and native query answers. Assignment observations do not establish declaration
initialization, exact element ancestry, unchanged contents or semantic lifetimes.
