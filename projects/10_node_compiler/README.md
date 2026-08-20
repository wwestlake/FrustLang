# node_compiler

Graph JSON -> real `.frust` source text. Nodes are data, never executed
directly - a graph description compiles down to genuine source that runs
through the exact same pipeline as any hand-written `.frust` file (parse
-> AST -> LLVM IR via `frust_lang`), loadable through
[`frust_plugin_host`](../09_frust_plugin_host) exactly like a
hand-written plugin. There is no separate graph-interpreter runtime.

See [`include/node_compiler/NodeCompiler.h`](include/node_compiler/NodeCompiler.h)
for the full graph JSON schema. Short version: nodes reference each
other's outputs by id (`{"ref": "other_node"}`) or a function parameter
(`{"param": "x"}`); every node becomes one `let <id>: <type> = <expr>;`
binding, in topologically-sorted order (a real DAG, not just a tree - a
node with multiple consumers is bound once, not re-evaluated per use).

```c
char* source = node_compiler_compile(graphJsonString);
if (!source) { /* node_compiler_last_error() */ }
// write `source` to a .frust file, load it via frust_plugin_host
node_compiler_free_string(source);
```

**The generated source is always validated before it's returned** -
`CompileGraphToSource` parses and codegens it through `frust_lang`'s
real pipeline (not a syntax guess) before reporting success, so a graph
that produces something that doesn't actually compile comes back as a
real error, not source the caller has to discover is broken later.

## v1 node types

- `literal_i64` / `literal_f64` / `literal_bool` / `literal_string`
- `add` / `sub` / `mul` / `div` / `mod` (numeric, matches Frust's own
  int/float auto-promotion)
- `eq` / `neq` / `lt` / `gt` / `le` / `ge` -> `bool`
- `if` - `[cond, then, else]`, maps directly to Frust's `if/else`
- `call` - calls an **extern** (host-registered or process-exported)
  C-ABI function; the compiler emits the `extern fn` declaration itself.
  Not for calling other Frust-source functions - `frust_plugin_host` is
  single-file only.
- `print` - sugar for `frust_print_str(frust_format_TYPE(x))` - a plugin
  using this needs the host to provide those two runtime symbols (either
  process-exported, like `frust_compiler.exe` already does, or
  registered via `frust_plugin_register_host_function` - see
  `examples/run_examples.cpp` for a generic host doing exactly that).

Not in v1, named explicitly (see the session plan): loops, node-defined
structs, multi-graph composition (a graph calling another graph as a
sub-node).

## Verification

`examples/run_examples.cpp` compiles three real graphs
(`simple_arithmetic.json`, `branch.json`, `call_and_print.json`), loads
each generated source via `frust_plugin_host`, calls it, and checks the
result against the mathematically expected value - not "does it
compile," "does it produce the right answer." All four checks (one
arithmetic, two branch directions, one external-call-plus-print)
verified passing.

## Scope

This repo delivers the graph schema and the compiler - **not** a visual
editor. Creation Suite (a separate codebase) owns building the actual
node-graph UI on top of this; this library's job ends at "graph JSON
in, compilable `.frust` source out."
