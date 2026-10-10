# frust-node-graph

The FRust node graph as a document, independent of any editor: node
definitions, the graph model, schematic JSON load/save and structural
validation. Links only `juce_core` - no LLVM, no compiler, no GUI - so the
FrustIDE node designer, command-line tools and other applications can load,
inspect, change and save the same graphs.

- `NodeDefinitions.h` - node types with typed pins (data, exec, stream,
  resource) and parameters. This is the table the IDE's node designer builds
  its palette from; colours and layout stay in the editor.
- `NodeGraph.h` - `Graph` (nodes, connections, graph inputs/output,
  subgraph functions), `parseGraph` / `loadGraph`, `toJson` / `saveGraph`,
  `validate`.

`node_compiler` (10_node_compiler) stays the compiler: this library never
generates code, and `validate` checks structure only, not what a backend
can lower.

## File format

The node schematic JSON described in
[`10_node_compiler/NODE_SCHEMATIC_SCHEMA_V2.md`](../10_node_compiler/NODE_SCHEMATIC_SCHEMA_V2.md).
Read: schemaVersion 1 (the node_compiler format), 2 (what the IDE wrote)
and 3. Written: 3, which adds one section:

```json
"connections": [
  { "id": "choose.false->say.in",
    "from": { "node": "choose", "pin": "false", "index": 1 },
    "to":   { "node": "say", "pin": "in", "index": 0 } },
  { "id": "x.x->sum.a", "from": { "input": "x", "pin": "x", "index": 0 },
    "to": { "node": "sum", "pin": "a", "index": 0 } }
]
```

A wire names the output pin it leaves (versions 1 and 2 only named the
node, so wires from a second or third output were lost), an input may take
several wires where its node allows it (a state entered by several
transitions), and ids are stable: derived from the wire's ends unless the
file gives one. Every node's `inputs` array is still written
(`{"ref": node, "pin": name}` / `{"param": name}` / `{"default": value}`),
so node_compiler reads a version 3 file as before.

Positions (`x`, `y`) are editor layout and never affect execution. Sections
and node keys the model does not interpret are saved back unchanged.
Documents that are not JSON objects, or that declare a newer schema
version, are refused with a reason rather than misread.

A v1 document whose `print` nodes use node_compiler's one-input shape is
written back as schemaVersion 1 so that shape keeps its meaning.

## Tools

- `frust_node_graph_tests` - regression tests, including the four
  node_compiler examples and an IDE-saved file (`tests/fixtures`).
- `frust_node_graph_inspect <file> [--resave <out>]` - load a schematic
  without a window, print its interface, nodes and connections, and
  validate it.

Both build to `bin/Debug` of the repository.
