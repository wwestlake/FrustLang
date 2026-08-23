# Frust node language: design + attack plan

**Standing instruction, same as `LANGUAGE_GAPS.md`'s own: update an
item's status the moment it changes.** This document exists because a
long design conversation is easy to lose once it's only ever lived in
chat history - the whole point is that it stays current, not that it
gets written once and goes stale.

## Audience and philosophy (read this first - it's the test for every
## future "should this be a node" question)

**The node graph is for domain-oriented people - artists, UX/sound/
level designers - not for programmers.** Anything that requires
thinking in types, closures, generics, or lambdas stays in
hand-written Frust, written by whoever's doing backend/systems work.
The node system's entire job is to let a non-coder wire together
pre-built behaviors without ever touching that layer. This is *why*
generics/closures/lambdas were dropped from scope early in this
design pass - not a limitation, the actual point. Every future
"should X be a node type" question gets answered by: does a domain
expert need to reach for this directly, or is it a backend concern
that belongs in the Frust code a node calls into?

## Core architecture

- **Nodes are not generated from a visual description - they're a
  reflection of real, already-written, already-compiled Frust code.**
  You don't draw a node and have the system invent Frust for it. You
  (or a host app developer, or the standard library) write a real
  Frust `fn`/`struct`, and the system says "this is a node, here's how
  to draw it" - inputs, outputs, and execution-return-paths read off
  the real, compiled declaration.
- **Discovery is via reflection over compiled metadata, not source
  scanning, and not a hand-authored manifest string.** A separate
  self-reported JSON description (the shape the plugin `manifest`
  system uses today) can drift out of sync with the real code. A
  node's shape must be derived structurally from the compiler's own
  knowledge of the declaration (its real interface method signature,
  its real struct fields) - this doubles as the reason to finally build
  real interface/struct reflection, a gap already named and deferred
  once before (see "Relationship to existing work" below).
- **Compiling a diagram generates CALLS into already-compiled Frust,
  never fresh logic.** The graph compiler's job is topology -> glue
  code, not "understand what `+` means" the way today's
  `10_node_compiler` hardcodes it.
- **Nodes never execute directly.** A compiled diagram becomes real
  Frust source/AST, JIT'd through the exact same pipeline
  `frust_plugin_host` already runs today. There is no graph
  interpreter, now or ever - this was true of `10_node_compiler`
  already and stays true here.

## Node shape model (grounded in how UE4/Blueprint actually does this -
## researched, not guessed, see the design conversation for sources)

Unreal's actual mechanism: `UFUNCTION`/`UPROPERTY`/`UCLASS` annotate
C++ declarations; the Unreal Header Tool harvests that into the
reflection system at compile time; Blueprint reads the *reflection
data*, never the raw C++ text. `BlueprintCallable` = exec pins,
imperative, explicitly wired into the flow, can have side effects.
`BlueprintPure` = no exec pins, wired only to data pins, evaluated
lazily/pulled on demand exactly when a downstream node needs the
value, called once per connection (no caching). Multiple named
exec-out paths (a node with "Success"/"Failure" exits) come from an
enum-shaped signal (`ExpandEnumAsExecs`), not multiple return values.

Frust equivalent, three real node kinds:
1. **Callable** - exec-in/exec-out pins, imperative, side-effecting.
   The Frust equivalent of `BlueprintCallable`.
2. **Pure** - no exec pins, data pins only, pulled on demand. The
   Frust equivalent of `BlueprintPure` - this is literally "information
   that's available to pull forward if needed" from the original
   framing of this whole design.
3. **Loop** - a genuinely different pin shape from either of the
   above: one exec-in, a "body" exec-out that fires once per
   iteration, and a "completed" exec-out that fires once after the
   loop ends (matches Blueprint's `ForEachLoop`). Not modelable as a
   plain callable/pure node - needs its own annotation shape.

## Two-tier node registry

- **System nodes**: math, `Vector<T>`/array indexing and iteration,
  branch, loop - general enough that any host benefits. Ship as real,
  ordinary annotated Frust functions (standard library or a
  node-oriented slice of it), discovered by the exact same reflection
  mechanism as host nodes - not hardcoded specially anywhere.
- **Host nodes**: app-specific (e.g. an IDE's `OpenReadSourceFile`) -
  supplied by whatever C++ app embeds the system. A host app links
  `frust_plugin_host`, registers its own domain functions via the
  *already-existing* `frust_plugin_register_host_function`, and a thin
  annotated Frust wrapper around each one becomes the node. The JUCE
  IDE is the *first* such host, not a special case - any app that
  links `frust_plugin_host` and registers functions gets the same node
  system wired to its own domain for free.

## Relationship to existing work (don't rebuild what's already real)

- `frust_plugin_host`'s host-function registration and JIT pipeline
  are exactly the mechanism host nodes call into and the mechanism a
  compiled diagram ultimately runs through - already built, already
  proven, reused as-is.
- **Real interface method-signature reflection** was named and
  deliberately deferred once already, in the original plugin/host
  completion plan ("no interface method-signature reflection - a host
  must already know an interface's shape ahead of time... real
  feature, bigger than the rest of this list, scheduled as its own
  follow-on"). This design makes that follow-on load-bearing, not
  optional - node discovery depends on it directly.
- `projects/01_language_paradigms/02_functional/grammar/frust.y`
  already has a `component`/`in`/`out`/wiring construct
  (`component Gain(f: f32) : AudioProcessor { in s; out o; o = s * f }`)
  that LOOKS like a natural fit for node-shaped code - but it has
  **zero codegen today** (parsed into `ComponentDecl`, never compiled
  in `Codegen.h`). **Open question, not yet resolved** (see item 0
  below): does the node system target `component` as its underlying
  construct (meaning `component` needs real codegen built first), or
  does it stay independent (annotated plain `fn`/`struct`, `component`
  remains a separate, unrelated gap)? Don't assume either answer -
  this needs a real decision before item 1 locks in the annotation
  syntax.
- `projects/10_node_compiler` (existing, real, shipped - graph JSON ->
  `.frust` source text, verified via 3 example graphs) needs
  fundamental rearchitecting, not incremental extension: away from
  "hardcode every node type as a C++ dispatch case generating raw
  expressions" toward "generate calls into pre-compiled,
  reflection-discovered node functions", and away from
  text-generation-then-reparse toward direct AST construction (already
  agreed as a real improvement independent of everything else here -
  it already links `AST.h`/`Codegen.h`, no new dependency needed).

## Numbered plan, in attack order (dependency-driven, not severity-driven)

### 0. Resolve the `component` question
**Status: OPEN - blocks item 1's annotation syntax design.**
Decide whether node-shaped Frust code is authored as `component`
declarations (requiring real `component` codegen as a prerequisite) or
as annotated plain `fn`/`struct` declarations (leaving `component` a
separate, unrelated gap). A real design call, not a mechanical one -
needs a real conversation, not a unilateral pick.

### 1. Node annotation syntax + real interface/struct reflection
**Status: OPEN.**
The foundation everything else needs. Two parts: (a) new Frust-side
syntax marking a declaration as node-shaped (callable/pure/loop -
exact spelling depends on item 0's answer); (b) a real reflection API
that can answer "what are this interface's method signatures" / "what
are this struct's fields" programmatically from compiled metadata -
the previously-deferred gap, now load-bearing.

### 2. Callable/Pure node kinds + multi-exec-out paths
**Status: OPEN. Depends on #1.**
Formalize the two base node kinds and how a branch-like node declares
multiple named exec-out paths (an enum-shaped signal, matching
`ExpandEnumAsExecs` - real design detail, not yet worked out how this
looks in Frust source).

### 3. Loop node shape
**Status: OPEN. Depends on #2's pin-modeling groundwork.**
The third, structurally different node kind (entry/body/completed
pins) - loops were explicitly out of scope for `10_node_compiler`'s
v1 and are needed for real.

### 4. System node library
**Status: OPEN. Depends on #1-#3.**
A real, annotated Frust "node stdlib" - math, `Vector<T>`/array
indexing+iteration, branch, loop - verified discoverable via #1's
reflection, not hardcoded into the compiler.

### 5. Host-node integration pattern
**Status: OPEN. Depends on #1 and #4's established pattern.**
Formalize the thin-wrapper pattern: a host-registered C++ function
(`frust_plugin_register_host_function`) gets an annotated Frust
wrapper function, which becomes a discoverable node. Verify with a
real example host exposing at least one domain-specific node (e.g.
this IDE's own `OpenReadSourceFile`).

### 6. Graph-to-glue-code compiler rearchitecture
**Status: OPEN. Depends on #1-#5 (needs real discoverable nodes to
call before it can generate calls to them).**
Rework `10_node_compiler` away from hardcoded node-type dispatch
toward: given a wiring diagram over known, reflection-discovered
nodes, generate the calling glue (topological order, exec-flow
sequencing, data threading) - AND switch from text-generation+reparse
to direct AST construction. Existing topo-sort/call-emission logic is
a real, reusable starting point, not a throwaway.

### 7. Visual canvas (JUCE panel)
**Status: OPEN. Deliberately last - do not start before #6's
diagram->code path is proven, same reasoning
[[project_frust_node_language_goal]]'s original Phase 1/Phase 2 split
already established ("no point building a visual canvas for a graph
model that doesn't convert cleanly yet").**
The actual drag/wire/draw UI, reading the discovered node registry
from #1-#5, producing whatever graph description #6 compiles.

## Verification discipline

Same as every other feature this session: design/state the call for
any real decision, implement, build single-core, write a real
hand-predicted test before calling anything done, full regression
sweep across whatever this touches (`10_node_compiler`'s existing 3
example graphs must keep passing at minimum), commit+push proactively
once verified. No step gets marked DONE on "should work."
