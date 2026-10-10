# Node Schematic Schema v2

Product requirements live in
[`NODE_DESIGNER_REQUIREMENTS.md`](NODE_DESIGNER_REQUIREMENTS.md). This
file is the current schema direction that lowers those UI and product
requirements into JSON.

This file defines the richer schematic model the IDE should grow into.
The node system is a general data-flow and execution-flow graph system.
It is not inherently a Frust system, a shader system, or an audio system.
Those are targets/backends.

The schematic is still data. Compilation produces a target artifact such
as Frust source, GLSL source, or another backend-specific output.

## Top-Level Shape

```json
{
  "schemaVersion": 2,
  "namespace": "djehuti.tools.example",
  "diagramType": "node_graph",
  "targets": ["frust"],
  "targetOptions": {},
  "executionContexts": [],
  "functionName": "compute",
  "params": [{ "name": "x", "type": "i64" }],
  "output": "result",
  "accessibility": "graph",
  "agentAccess": {},
  "events": [],
  "messages": [],
  "asyncOperations": [],
  "states": [],
  "transitions": [],
  "variables": [],
  "constants": [],
  "structs": [],
  "enums": [],
  "groups": [],
  "subgraphs": [],
  "nodes": [],
  "debug": {}
}
```

`functionName`, `params`, `output`, and `nodes` remain compatible with
v1. New fields are additive.

## Diagram Types

`diagramType` selects the graph language used by the editor and compiler
frontend. It is separate from `target`; a state machine may still target
Frust, and a node graph may target Frust, GLSL, or a future backend.

Known diagram types:

- `node_graph`: data-flow/execution-flow node language.
- `state_machine`: state/event/transition language.
- future: `shader_graph`, `dsp_graph`, `ui_flow`.

Backends should reject diagram types they do not lower yet instead of
guessing. The IDE may still save/load and display an unsupported diagram
type as schematic data.

## Namespaces and Packages

A schematic is not an anonymous drawing. It belongs to a namespace and
may compile to a package/module/application artifact.

```json
{
  "namespace": "djehuti.station.mixer",
  "package": {
    "name": "station_mixer_tools",
    "version": "0.1.0",
    "kind": "application"
  }
}
```

Backends lower this into their own module/package system. For Frust this
means generated Frate metadata, imports, source files, and eventually
module paths. The visual editor must not encode Frust package details
directly; it records namespace/package intent and lets the backend emit
the target artifact.

## Source of Truth and Compile Targets

The schematic JSON is the source document. Generated code is a build
artifact. A schematic may be compiled to one or more targets:

- `frust`: ordinary Frust source, used for application logic, tools,
  plugins, DSP devices, game logic, and general compiled behavior.
- `glsl`: GLSL shader source, used only for shader-compatible node
  graphs.
- future targets may include HLSL, WGSL, C++, or host-specific graph
  packages.

The target is chosen by use case:

- Application/game/tool logic: usually `frust`.
- Shader/material graph: usually `glsl`, `hlsl`, or `wgsl`.
- DSP graph: likely `frust` first, later a DSP-optimized backend.
- Editor automation graph: likely `frust` with host services.
- Data transform graph: `frust`, SQL-like targets, or future batch
  backends depending on context.

Target selection belongs in the schematic:

```json
{
  "targets": ["frust", "glsl"],
  "targetOptions": {
    "frust": {
      "functionName": "compute"
    },
    "glsl": {
      "stage": "fragment",
      "entryPoint": "mainImage"
    }
  }
}
```

Each backend validates that the graph only uses node kinds, types, side
effects, and resources legal for that target. A graph with file I/O or
stateful host calls may be valid for `frust` and invalid for `glsl`. A
shader math graph may be valid for both.

## Flow Model

The graph has two related flow systems:

### Data Flow

Data-flow pins carry values. Pure nodes participate only in data flow.
They have inputs and outputs but no execution pins.

Examples:

- math
- color composition
- vector transforms
- string formatting
- struct construction
- pure queries

### Execution Flow

Execution-flow pins describe ordering, branching, looping, side effects,
and stateful work. Callable nodes have execution pins. Loop nodes have
special execution shapes such as body/completed.

Examples:

- open file
- play sound
- spawn actor
- write asset
- send host command
- branch/sequence/loop

Some targets support only data flow. Shader targets are mostly data-flow
graphs with restricted control structures. Frust targets can support both
data flow and execution flow.

The schema must grow toward explicit typed pins. A backend must not infer
that every wire means the same thing. Data, execution, event, state,
resource, stream/time, and control-flow lanes are separate semantic
concepts even if the visual editor draws them as wires.

Early v2 schematics may still use the v1 `inputs` array for compatibility
with the current Frust backend. Rich pins should be added additively so
old graphs still compile.

## Execution Contexts, Threads, and Concurrency

Execution contexts describe where work is allowed to run. They are part
of the schematic language because real applications have UI threads,
audio/render threads, workers, services, and background task pools.

```json
{
  "executionContexts": [
    {
      "id": "main",
      "name": "Main Thread",
      "kind": "main_thread",
      "allowsBlocking": true,
      "allowsAllocation": true
    },
    {
      "id": "audio",
      "name": "Audio Thread",
      "kind": "realtime_thread",
      "allowsBlocking": false,
      "allowsAllocation": false
    },
    {
      "id": "worker",
      "name": "Worker Pool",
      "kind": "task_pool",
      "allowsBlocking": true,
      "allowsAllocation": true
    }
  ]
}
```

Nodes that perform work may declare a context:

```json
{
  "id": "render_block",
  "type": "process_audio_block",
  "context": "audio",
  "inputs": []
}
```

Backends validate concurrency rules. For example, a Frust backend may
reject blocking file I/O in an audio context, require copied payloads
when crossing from UI to worker context, or generate message passing
instead of direct shared mutation.

## Async Operations

Async operations are first-class semantic constructs, not hidden helper
calls. A graph may start work, await/join work, handle success/failure,
cancel it, or time it out.

```json
{
  "asyncOperations": [
    {
      "id": "load_asset",
      "name": "Load Asset",
      "requestType": "AssetLoadRequest",
      "resultType": "AssetLoadResult",
      "runsOn": "worker",
      "completionContext": "main",
      "cancellable": true,
      "timeoutMs": 5000
    }
  ]
}
```

Async nodes lower differently per target. Frust may emit task handles,
callbacks, channels, or runtime service calls. Shader targets generally
reject async constructs.

## Events and Messages

Events describe something that happens. Messages describe typed payloads
that cross a boundary. Both are part of the application model.

```json
{
  "messages": [
    {
      "id": "track_gain_changed",
      "name": "TrackGainChanged",
      "fields": [
        { "name": "trackId", "type": "String" },
        { "name": "gainDb", "type": "f64" }
      ],
      "delivery": "queued"
    }
  ],
  "events": [
    {
      "id": "on_track_gain_changed",
      "name": "On Track Gain Changed",
      "message": "track_gain_changed",
      "context": "main",
      "accessibility": "project"
    }
  ]
}
```

Messages may be synchronous, queued, broadcast, request/response, or
streaming depending on the runtime. The schematic records intent; the
backend emits Frust structs/enums, channels, service hooks, or host API
bindings as appropriate.

## State Machines

A state-machine diagram is a sibling graph language to the ordinary node
graph. It models behavior over time through states, events, guards, and
transitions. Node graphs may be attached as entry/update/exit actions,
transition actions, or boolean guards.

```json
{
  "diagramType": "state_machine",
  "name": "RecordingAssistant",
  "events": [
    { "id": "push_to_talk", "name": "PushToTalkPressed" },
    { "id": "speech_done", "name": "UserFinishedSpeaking" }
  ],
  "states": [
    {
      "id": "idle",
      "name": "Idle",
      "initial": true,
      "entryAction": "restore_audio_routing",
      "x": 80,
      "y": 160
    },
    {
      "id": "listening",
      "name": "Listening",
      "entryAction": "route_microphone_to_engineer",
      "x": 330,
      "y": 95
    }
  ],
  "transitions": [
    {
      "id": "idle_to_listening",
      "from": "idle",
      "to": "listening",
      "event": "push_to_talk",
      "guard": "",
      "action": ""
    }
  ]
}
```

State machine lowering to Frust produces a real Frust pod artifact. The
first implementation is deliberately integer-backed for compatibility
with the current compiler/runtime:

- `state_<id>() -> i64` functions define stable state ids
- `event_<id>() -> i64` functions define stable event ids
- `initial_state() -> i64` returns the starting state
- `state_name(state: i64) -> String` supports simple diagnostics and demos
- `step(current_state: i64, event: i64) -> i64` dispatches transitions

That gives the IDE a compileable artifact now while preserving the
schematic JSON as the authoritative model. Enum-backed lowering is the
next refinement once the target enum ABI and exported API shape are firm.

Longer-term state machine lowering to Frust should produce:

- a state enum
- event/message types
- a context struct for variables/resources
- a transition dispatcher
- entry/update/exit/transition action calls
- source/debug maps for current-state highlighting, breakpoints, and
  transition watches

The editor should show states as boxes and transitions as arrows. The
properties panel edits selected state/transition metadata, while action
and guard fields reference node functions or embedded node graphs.

## Variables, Constants, and Events

Variables, constants, and events are first-class schematic concepts, not
loose comments in the graph.

```json
{
  "events": [
    {
      "id": "evaluate",
      "name": "Evaluate",
      "accessibility": "graph",
      "payload": [{ "name": "x", "type": "i64" }]
    }
  ],
  "variables": [
    {
      "id": "last_result",
      "name": "last_result",
      "type": "i64",
      "accessibility": "private",
      "persistent": false
    }
  ],
  "constants": [
    {
      "id": "offset",
      "name": "offset",
      "type": "i64",
      "value": 10,
      "accessibility": "graph"
    }
  ]
}
```

Backends validate whether a variable, constant, or event can be emitted
for the selected target.

## Accessibility and Agent Access

Schematic artifacts can describe who can see or use them:

- `private`
- `graph`
- `module`
- `project`
- `public`
- `agent`
- `readonly`
- `hidden`

Agent access is explicit:

```json
{
  "agentAccess": {
    "visible": true,
    "editable": true,
    "requiresApproval": false,
    "notes": "Agent may refactor this graph but must preserve public outputs."
  }
}
```

Agents should edit the schematic source of truth and let targets generate
artifacts. Direct target-code editing should be reserved for explicit
user requests or backend repair work.

## Backend Contract

A backend consumes the same schematic model:

1. Validate graph structure.
2. Validate target-specific node legality.
3. Infer boundary inputs and outputs.
4. Topologically order pure/data dependencies.
5. Preserve execution flow for callable/stateful graphs.
6. Emit target source.
7. Optionally run target compiler validation.

No backend should require the visual editor to know target-specific code
syntax. The editor edits nodes and schematics; backends emit code.

## Comment Groups

Comment groups are editor metadata. They do not affect generated code.

```json
{
  "groups": [
    {
      "id": "damage_math",
      "title": "Damage Math",
      "comment": "Base damage plus combo multiplier.",
      "nodes": ["base", "combo", "result"],
      "color": "#24445a"
    }
  ]
}
```

The IDE draws these as colored comment boxes behind nodes. Frusty may
create them to make generated schematics readable.

## Node Functions

A node function is a package-owned callable graph definition. It may be
local to the schematic package, exported from that package, or imported
from another pod. Once a function is visible to a diagram, the editor may
place it as a callable node.

Functions are not owned by the canvas they were created on. If a user
selects nodes in `MainDamageFlow` and converts them to a function, the
new function is added to the package's `subgraphs` collection and the
selected nodes are replaced with a call node referencing that function.

```json
{
  "subgraphs": [
    {
      "id": "score_bonus",
      "name": "ScoreBonus",
      "namespace": "game.combat",
      "kind": "pure",
      "accessibility": "package",
      "inputs": [{ "name": "score", "type": "i64" }],
      "outputs": [{ "name": "bonus", "type": "i64" }],
      "nodes": [
        { "id": "ten", "type": "literal_i64", "value": 10 },
        { "id": "bonus", "type": "add", "inputs": [{ "param": "score" }, { "ref": "ten" }] }
      ],
      "return": { "ref": "bonus" }
    }
  ]
}
```

Kinds:

- `pure`: data-only function, no execution pins, no side effects.
- `callable`: stateful/imperative function with an execution line.
- `macro`: inline expansion; useful for collapsing visual clutter
  without creating an externally callable function.

Accessibility controls who may call the function:

- `private`: only implementation graphs inside this package.
- `package`: all diagrams/functions in this package.
- `project`: other packages in the current project may call it.
- `public`: exported as part of the pod's public surface.

## Function References

A function-call node references a resolved function symbol. The function
may be local to the same package or provided by a dependency pod.

```json
{
  "id": "apply_gain",
  "type": "call_function",
  "function": {
    "namespace": "djehuti.audio",
    "name": "ApplyGain",
    "pod": "djehuti_audio_nodes",
    "version": "1.0.0"
  },
  "inputs": [
    { "ref": "sample" },
    { "ref": "gain" }
  ]
}
```

For a local function, `pod` and `version` may be omitted:

```json
{
  "type": "call_function",
  "function": {
    "id": "score_bonus",
    "namespace": "game.combat",
    "name": "ScoreBonus"
  }
}
```

The backend resolves the function reference, verifies accessibility, adds
the owning pod dependency when needed, imports public types/resources,
and emits the target call.

## Public Surface and Resources

Pods expose a public surface. Functions are only one part of that
surface; their signatures may require public types, constants, messages,
events, or runtime resources.

```json
{
  "resources": [
    {
      "id": "audio_buffer",
      "name": "AudioBuffer",
      "kind": "stream_resource",
      "accessibility": "public"
    }
  ],
  "exports": {
    "functions": ["djehuti.audio.ApplyGain"],
    "types": ["djehuti.audio.AudioBlock"],
    "messages": ["djehuti.audio.RenderBlock"],
    "resources": ["AudioBuffer"]
  }
}
```

The compiler must treat these as contracts. A graph that calls an
external function needs the owning pod and its exported public resources
available; private implementation resources remain hidden.

## External Function Extraction

When the user says "collapse to function" or "make this a function",
or selects nodes and chooses "Convert to Function", the IDE should
create a `subgraph` and then compile it as one of:

- pure Frust function
- callable/stateful Frust function
- macro-like inline graph

The extraction operation must infer inputs/outputs from boundary wires:

- Inputs are external params/nodes consumed by the selected group.
- Outputs are selected nodes consumed outside the group or selected as
  the group's declared output.
- Stateful extraction preserves execution ordering and any exec pins.
- The original graph receives a `call_function` node with the same
  boundary pins.
- Debug markers and source maps are remapped from moved nodes to the new
  function where possible.

## Structs

Struct definitions are schematic-owned data models and compile to real
Frust structs.

```json
{
  "structs": [
    {
      "name": "DamageContext",
      "fields": [
        { "name": "base", "type": "i64" },
        { "name": "combo", "type": "i64" }
      ]
    }
  ]
}
```

## Enums

Enum definitions are schematic-owned decision models and compile to real
Frust enums once enum codegen is fully ready for this path.

```json
{
  "enums": [
    {
      "name": "DamageKind",
      "variants": ["Normal", "Critical", "Blocked"]
    }
  ]
}
```

The v2 compiler validates enum names and emits enum definitions only
when the language path supports them. If that path is not ready, the IDE
must keep enum editing as schema data and report that enum compilation is
not available yet.

## Node Positions

Nodes may include editor position data:

```json
{ "id": "result", "type": "mul", "x": 420, "y": 120, "inputs": [] }
```

Positions are optional. If absent, the IDE auto-lays out nodes.

## Debug Markers

Diagram-level debugging stores user intent in the schematic. Target
debuggers consume backend debug/source maps to turn those markers into
real breakpoints, watches, and runtime highlights.

```json
{
  "debug": {
    "breakpoints": [
      {
        "id": "bp_001",
        "target": { "kind": "node", "nodeId": "validate_input" },
        "enabled": true,
        "condition": "",
        "hitCount": 0
      }
    ],
    "watches": [
      {
        "id": "watch_001",
        "target": { "kind": "pin", "nodeId": "parse_command", "pin": "output" },
        "enabled": true,
        "label": "Parsed command"
      }
    ]
  }
}
```

The visual designer may draw these as red breakpoint dots and watch
markers before any target debugger exists. Runtime behavior requires a
target debug map.

## Design Rule

Visual organization is metadata. Execution semantics compile to an
explicit target. No node graph should require a hidden interpreter to
run.
