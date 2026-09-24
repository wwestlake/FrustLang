# FRUST_AI_CONTEXT.md

This is the always-on context brief for the Frust IDE Agent. It is derived
directly from the Frust grammar (grammar/frust.y + grammar/frust.l) confirmed
by the build system (CMakeLists.txt). Do not treat any LLM-authored summary
document as authoritative over this file or over the grammar itself.

---

## What Frust Is

Frust is a statically-typed, expression-oriented language under active
development. It compiles via LLVM (JIT and AOT). Its build tool is `frate`.
It is Windows-only. There is no Linux/Unix target.

---

## What the Grammar Actually Defines

The following is derived from frust.y and frust.l as compiled by the build.
This is the ground truth. If something is not in the grammar, do not suggest it.

### Keywords

`fn` `extern` `entry` `use` `import` `if` `else` `while` `loop` `for`
`break` `continue` `impl` `interface` `manifest` `self` `pub` `unsafe`
`let` `mut` `return` `struct` `type` `effect` `perform` `handle` `resume`
`with` `component` `node` `pure` `callable` `in` `out` `build_time`
`quote` `unquote` `as` `own` `shared` `weak` `raw` `true` `false`
`enum` `match` `_`

### Operators and Punctuation

Binary: `+` `-` `*` `/` `%` `==` `!=` `<` `>` `<=` `>=` `<<` `>>` `&` `|` `^`
Unary: `-` `!` `*` (deref)
Assignment: `=`
Cast: `as`
Paths: `::` `::<` (turbofish) `.` `..`
Arrows: `->` `=>`
Delimiters: `{ }` `( )` `[ ]` `,` `;` `:`

### Literals

- Integer: digits
- Float: digits.digits
- String: "..." with \n \t \\ \" escapes
- Bool: `true` `false`

### Top-Level Declarations

```
program ::= decl* trailing_stmt?

decl ::= function_decl | struct_decl | enum_decl | type_alias_decl
       | effect_decl | component_decl | node_decl | use_decl
       | impl_decl | interface_decl | manifest_decl | stmt ";"
```

### Functions

```frust
pub? unsafe? extern? entry? fn name<T>(param: Type) -> RetType = expr
pub? unsafe? extern? entry? fn name<T>(param: Type) -> RetType ;
```

Body is a single expression (typically a block). Qualified names allowed:
`fn Result::Ok<T, E>(v: T) -> Result<T, E> = ...`

### Structs

```frust
struct Name { field: Type, ... }
struct Name<T> { field: T }
```

### Enums

```frust
enum Name { Variant, Variant(Type), Variant(Type, Type) }
enum Name<T> { Some(T), None }
```

### Impl Blocks

```frust
impl TypeName { fn method(&mut self, ...) -> T = expr }
impl<T> TypeName<T> { fn method(self) -> T = expr }
impl InterfaceName for TypeName { fn method(self) -> T = expr }
```

Self forms: `self` | `&self` | `&mut self`

### Interfaces

```frust
interface Name { fn method(&self) -> T }
```

### Type Aliases

```frust
type Name = TypeExpr
```

### Use / Import

```frust
use pod_name;
use self::module_name;
import pod_name, "version";
```

### Effects / Handle

```frust
effect EffectName(param: Type) -> RetType

handle expr with {
    effect EffectName(param) => expr
}

perform EffectName(args)
resume(value)
```

### Components and Nodes

```frust
component Name(param: Type) : InterfaceName {
    in port: Type;
    out port: Type;
    wiring_expr;
}

node pure fn name(param: Type) -> Type = expr
node callable fn name(param: Type) -> Type = expr
node loop fn name(param: Type) -> Type = expr
```

### Manifest

```frust
manifest "{ ...raw JSON... }";
```

---

### Type Expressions

```
own TypeName        // owned smart pointer
shared TypeName     // reference-counted
weak TypeName       // weak reference
&TypeName           // reference
raw* TypeName       // raw pointer
Name<T, U>          // generic instantiation
TypeExpr[lo..hi]    // refinement range
TypeExpr[op value]  // refinement comparison
```

Type args can be types or integer literals: `Vec<3>` `Array<f32, 256>`

---

### Statements and Expressions

Blocks return their last expression. Inner statements require `;` except the last.

```frust
let name = expr
let mut name: Type = expr
return expr
```

### Struct Literals In Call Arguments

Struct literals are valid expressions, but the current parser can misparse a struct
literal used directly as an argument before additional comma-separated arguments:

```frust
// Avoid in standalone code today: may produce
// "unexpected INT_LITERAL, expecting }" at the next argument.
check_case(ScoreInput { base_points: 10, combo_multiplier: 2, penalty: 50 }, 0, 0)
```

Bind the struct literal to a local first, then pass the local:

```frust
let input = ScoreInput { base_points: 10, combo_multiplier: 2, penalty: 50 };
check_case(input, 0, 0)
```

This pattern matches known-good Frust examples and avoids the call-argument ambiguity.

### Control Flow

```frust
if cond { ... }
if cond { ... } else { ... }
if cond { ... } else if cond { ... }
while cond { ... }
loop { ... }
for name in start..end { ... }   // integer range only
break
continue
```

**IMPORTANT**: `match` requires parens around the scrutinee:
```frust
match (expr) {
    Pattern => expr
    Pattern => expr
}
```

Patterns: `_` | literal | `binding` | `Enum::Variant` |
`Enum::Variant(p1, p2)` | `TypeName { field: pattern }`

### Closures

```frust
|param: Type| -> RetType { body }
|| -> RetType { body }
```

By-value capture only. Must be `let`-bound to call.

### Smart Pointer Construction

```frust
own StructName { field: expr }
shared StructName { field: expr }
raw StructName { field: expr }
```

### Turbofish

```frust
fn_name::<Type>(args)
```

### Cast

```frust
expr as Type
```

---

## Frate (Build Tool)

Pods use `frate.json`. Entry points: `src/main.fr` (executable), `src/lib.fr` (library).

```
frate build
frate run
frate package
frate install-local
frate update
frate cache-dir
```

### Frate Build Smoke Tests

For `frate build` executable smoke tests, prefer a deterministic numeric entry point:

```frust
use core;

fn main() -> i64 = {
    0
}
```

Do not use `println_str`, `println_i64`, `println_f64`, `println_bool`, `core::println_str`,
or `core::println_*` as proof that a standalone `frate build` executable works unless you
have first verified that console I/O is linkable in that build path. The current `core`
`console_io` helpers are documented as JIT-only host exports. They can appear in examples
and source context, but they may fail standalone Frate builds with `unknown function` or
link/runtime diagnostics. If the user explicitly asks for printed output from a standalone
build and console I/O is unavailable, report that as a capability/runtime gap instead of
looping on syntax changes.

---

## Hard Constraints

- Windows only. No Linux/Unix paths, commands, or conventions.
- Ground truth is the grammar (frust.y/frust.l), not any doc file.
- Do not suggest syntax absent from the grammar.
- Do not suggest features without confirmed codegen support.
- Passing smoke tests outweigh any prose document.
- Generated build products (.ll files, IR dumps) are not source truth.

---

## Agent Runtime Contract

- The host-supplied provider, exact model ID, application, and assigned role are authoritative runtime identity. Never infer model identity from application branding, project names, retrieved documents, or conversational style.
- The host-owned task packet is authoritative for the current goal, mode, phase, plan, and evidence.
- `Auto` is a routing stage, not a work mode. A small tool-free classification request chooses Answer, Review, Plan, or Execute before LiteSemRAG context and engineering tools are attached.
- Explicitly selected Review, Plan, and Execute modes bypass Auto routing and remain authoritative.
- Output detail (Brief, Standard, or Detailed) controls presentation only. It never changes mode, access, approvals, or completion requirements.
- `Plan` inspects and records a concrete plan but never writes.
- `Execute` follows inspect, plan, implement, verify, and finish phases.
- `Review` inspects and reports findings but never writes.
- Access is separate from mode. Workspace access is required for edits.
- A workspace can contain one writable project root and multiple read-only reference roots. Absolute reads outside those roots ask the user to allow one read or open a persistent read-only tree.
- Full Access suppresses ordinary path, command, and launch confirmations within the Windows account's permissions, but never enables writes in Plan or Review and never overrides a read-only reference root or the host's hard-deny rules.
- Use project tools for real work. A Markdown code block is not a file update.
- Begin every active task with `workspace_list` at the open project root so duplicate or misplaced project structures are visible before planning.
- After inspection, call `agent_assess_capabilities` with required, available, and missing prerequisites plus concrete evidence. The feature being requested is work to perform, not a missing prerequisite. A missing prerequisite requires a user decision before implementation.
- Unknown external runtime status is not automatically a missing prerequisite. If the host grants `run_command`, checks such as `dotnet --version`, `cmake --version`, `node --version`, or `frate --version` belong in the plan and acceptance tests. Record the shell/tool-check capability as available, then verify the runtime during implementation. Pause only if the command is unavailable, the runtime is proved absent, and there is no acceptable fallback in the user's request.
- Before inventing a module, build, test, package, or publication pattern, inspect a known-good precedent.
- `agent_set_plan` must record constraints and executable acceptance tests as well as implementation steps.
- Preserve the requested language, architecture, and scope. Do not substitute a script, launcher, another language, or reduced behavior without the user's decision.
- Use `workspace_write_file` for a whole-file repair after reading the current file. Supply the SHA-256 revision from `workspace_read`; stale writes are rejected.
- Use workspace tools to edit source and documentation. PowerShell is for inspection, builds, tests, packaging, publication, and approved operations, not source rewriting.
- After changing Frust source, call `workspace_check_frust`, repair diagnostics, and check again.
- Treat syntax check, full build/link, automated tests, runtime acceptance, and publication as separate evidence gates. Satisfy every gate requested by the task in that order.
- If the same underlying failure occurs twice, investigate the root cause instead of making another surface edit. The host pauses the task on the third recurrence.
- Do not version, package, install, or publish incomplete work. Publication is last and only after the real acceptance tests pass.
- Finish an active run only through `agent_complete_task`. The host rejects premature completion.
- Use `agent_request_user` only for information or judgment unavailable from project context or tools.
