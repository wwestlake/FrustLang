# Node Designer Requirements

## Purpose

The node designer is a semantic graph authoring environment for
DjehutiSuite systems. It is not just a drawing surface and it is not
owned by any one target language.

The user works in a visual UI first. The UI lowers to canonical schematic
JSON. The schematic JSON lowers to one or more target artifacts such as
Frust source, GLSL shader source, DSP graphs, host automation packages,
or future backends.

```text
Visual Node Designer
        ->
Canonical Schematic JSON
        ->
Target Backend
        ->
Frust / GLSL / DSP / Host Automation / Runtime Package
```

The graph system is the product. Targets are outputs.

Production already contains a GLSL-producing node system. That system is
reference material and prior art. This research IDE work is free to
design a cleaner general graph model and target contract.

## Primary Users

- Domain users who think in behavior, data, media, shaders, audio, game
  logic, application actions, or workflows rather than source code.
- Technical users who want visual structure, debugging, and inspection
  over generated behavior.
- Agents such as Frusty that need to inspect, modify, explain, and
  validate schematics through explicit tools and stable data contracts.
- Host applications that need to expose domain-specific node libraries,
  services, resources, and target backends.

## Core Use Cases

### Author a Schematic Visually

The user creates a node schematic by placing nodes, wiring pins, adding
groups, defining variables/constants/events, and selecting target intent.

Acceptance criteria:

- The user can create nodes from an available node library.
- The user can wire typed pins only when the connection is legal.
- The user can arrange nodes, groups, and collapsed subgraphs visually.
- The visual arrangement serializes to canonical JSON.
- Generated code is treated as an artifact, not the source of truth.

### Compile a Schematic to a Target

The user selects a target such as Frust or GLSL and asks the system to
compile or validate the schematic.

Acceptance criteria:

- The backend validates graph structure.
- The backend validates target-specific node legality.
- The backend rejects unsupported flow kinds, node kinds, resources, or
  side effects with useful diagnostics.
- The backend emits target artifacts only when the schematic is valid for
  that target.
- The schematic remains editable even when a target rejects it.

### Work With Agentic Systems

The user can ask an agent to inspect, create, modify, debug, explain, or
refactor node schematics.

Acceptance criteria:

- The agent can read canonical schematic JSON.
- The agent can query available nodes, target capabilities, diagnostics,
  variables, constants, events, and debug markers.
- The agent can modify the graph through tools rather than by guessing
  target code.
- Schematic elements can declare whether they are visible to the agent,
  editable by the agent, read-only, hidden, or approval-gated.

### Debug at Diagram Level

The user can set breakpoints and watches directly on graph elements, then
see runtime execution reflected in the diagram.

Acceptance criteria:

- The user can set breakpoints on execution nodes and execution wires.
- The user can set watches on pins, variables, resources, and stream
  values where supported.
- Debug markers serialize into the schematic.
- Target backends emit debug/source maps connecting graph IDs to target
  artifacts.
- The underlying debugger can report current node, wire, pin values,
  watch values, breakpoint hits, and errors back to the designer.
- The designer can highlight active execution paths and display watched
  values without owning the target debugger itself.

## Flow Kinds

The schematic model must represent several distinct kinds of flow.
Different targets may support different subsets.

### Data Flow

Data-flow pins carry values between pure computations. They have no side
effects and no imperative ordering beyond dependency ordering.

Examples:

- math
- color composition
- vector transforms
- string formatting
- struct construction
- pure queries

### Execution Flow

Execution-flow pins represent imperative ordering, side effects,
branching, sequencing, and stateful actions.

Examples:

- validate input
- save file
- show notification
- spawn actor
- send host command

### Event Flow

Event flow starts execution from outside the graph or from a named graph
entry point.

Examples:

- button pressed
- timer tick
- MIDI command
- transport started
- asset loaded
- custom event

### State Flow

State flow represents persistent state reads and writes. State must be
explicit because it affects target legality, debugging, replay, and
agent reasoning.

Examples:

- current project
- selected object
- current track
- active preset
- cached analysis
- graph-local variable

### Resource Flow

Resource-flow pins carry handles or references to host-managed resources.

Examples:

- file handles
- textures
- shader samplers
- audio buffers
- video frames
- models
- sockets
- clips
- device handles

### Time and Stream Flow

Stream flow represents repeated processing over frames, blocks, samples,
ticks, or other time domains.

Examples:

- audio sample/block processing
- video frame processing
- animation tick
- DSP chain
- frame graph
- sensor stream

### Control and Decision Flow

Control flow represents decisions and iteration. Some targets support
rich control flow; shader targets may support only restricted forms.

Examples:

- branch
- match/switch
- sequence
- loop
- foreach
- guard

## Workspace UI Requirements

The node designer shall be a dockable workspace, similar in spirit to
Blender side panels. The canvas is the central surface, not the entire
tool.

Required panels:

- Canvas
- Available Nodes
- User Nodes
- Properties
- Variables
- Constants
- Events
- Target Output
- Diagnostics
- Agent Actions
- Schematic Metadata
- Access Control
- Debug

Panels shall be dockable, rearrangeable, and context-aware.

Panel contents shall respond to:

- selected node
- selected wire
- selected pin
- selected group
- selected variable
- selected constant
- selected event
- active diagram type
- active target
- current diagnostics
- current debug state
- current agent mode/access level

## Variables, Constants, and Events

The designer shall allow the user to define and manage variables,
constants, and events as first-class schematic concepts.

Variables:

- may be graph-local, subgraph-local, module-level, project-level, or
  public depending on accessibility;
- may be read-only, writable, transient, persistent, or host-bound;
- must expose type, default value, scope, accessibility, and target
  legality.

Constants:

- are named values used to avoid magic literals;
- may be private to a schematic or shared through broader scopes;
- should be available from a constants panel and searchable by agents.

Events:

- define graph entry points or externally triggered behavior;
- may be host-provided or user-defined;
- must declare payload pins and supported targets.

## Accessibility Model

Major schematic artifacts shall declare accessibility. This controls use
inside the designer, visibility to other code modules, and access by
agents.

Suggested accessibility values:

- `private`: visible only inside the defining subgraph or schematic.
- `graph`: visible anywhere in the current graph document.
- `module`: visible to other schematics or code files in the same module.
- `project`: visible project-wide.
- `public`: exported to external modules/plugins.
- `agent`: visible/actionable by the AI assistant.
- `readonly`: visible but not editable.
- `hidden`: internal implementation detail.

Accessibility applies to:

- variables
- constants
- events
- subgraphs
- user-defined nodes
- structs
- enums
- generated functions
- host services
- resource bindings
- debug markers

## Agent Access

Because DjehutiSuite systems are agentic, every schematic must have an
intentional agent surface.

Example:

```json
{
  "agentAccess": {
    "visible": true,
    "editable": true,
    "requiresApproval": false,
    "notes": "Agent may refactor this subgraph but must preserve public outputs."
  }
}
```

Agent-facing tools should prefer schematic edits over generated code
edits. Generated code may be inspected for diagnostics, but the schematic
is the source of truth.

## Debug Model

Debugging is represented at the diagram level but executed by target
debuggers.

The designer owns:

- placing debug markers;
- storing debug intent in schematic JSON;
- displaying current runtime state;
- highlighting active execution paths;
- showing watch values;
- enabling/disabling/removing breakpoints and watches.

The target debugger owns:

- setting real target breakpoints;
- stepping;
- continuing;
- reading runtime values;
- reporting current execution location;
- reporting exceptions/errors.

The bridge between the two is the target debug map.

Example debug intent:

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
        "target": {
          "kind": "pin",
          "nodeId": "parse_command",
          "pin": "output"
        },
        "enabled": true,
        "label": "Parsed command"
      }
    ]
  }
}
```

Example target debug map:

```json
{
  "debugMap": {
    "nodes": {
      "validate_input": {
        "target": "frust",
        "file": "generated/player_flow.fr",
        "line": 42,
        "symbol": "validate_input"
      }
    },
    "pins": {
      "parse_command.output": {
        "target": "frust",
        "file": "generated/player_flow.fr",
        "line": 38,
        "valueSymbol": "parse_command_result"
      }
    }
  }
}
```

Required visual indicators:

- red breakpoint marker on execution node or execution line;
- eye marker on watched pin;
- active node highlight;
- active wire highlight;
- watched value bubble or tooltip;
- error/exception marker on the graph element associated with the
  failure.

## Stable Identity Requirement

All meaningful graph elements must have stable IDs so agents, debuggers,
source maps, generated artifacts, and version control can track them.

Required stable IDs:

- schematic ID
- node ID
- pin ID
- connection ID
- group ID
- subgraph ID
- variable ID
- constant ID
- event ID
- debug marker ID

Targets and debuggers must never depend on visual order such as "third
pin on the second node."

## Target Capability Requirement

Each backend shall publish a capability description.

The capability description should include:

- supported flow kinds;
- supported node categories;
- supported data types;
- supported resources;
- supported control-flow forms;
- supported debug features;
- supported accessibility/export forms;
- unsupported features with explanation.

The UI, Frusty, and validators should use this capability description
before claiming a schematic can compile to a target.

## Open Questions

- What exact JSON shape should represent typed pins and multiple flow
  lanes?
- Should event entry points be nodes, top-level declarations, or both?
- How should graph-owned structs/enums map to Frust and shader targets?
- How should collapsed subgraphs differ from extracted functions and
  macros?
- How should target capability manifests be discovered by the IDE?
- How much of the production GLSL node system should be adapted versus
  replaced behind the new contract?
- Which debugger interface should be proven first: Frust, GLSL, or a
  simulated debug driver?
