// node_compiler - graph JSON -> real .frust source text.
//
// Nodes are DATA, never executed directly - a graph description compiles
// down to genuine .frust source (parse -> AST -> LLVM IR through the
// normal frust_lang pipeline, loadable via frust_plugin_host exactly
// like a hand-written plugin). There is no separate graph-interpreter
// runtime. The visual editor that produces this JSON is a separate
// concern (Creation Suite, not this repo) - this library's job starts
// at "graph JSON in" and ends at "compilable .frust source out."
//
// -----------------------------------------------------------------------
// Graph JSON schema (v1)
// -----------------------------------------------------------------------
//
//   {
//     "functionName": "compute",
//     "params": [ {"name": "x", "type": "i64"} ],
//     "output": "result",
//     "nodes": [
//       {"id": "ten", "type": "literal_i64", "value": 10},
//       {"id": "sum", "type": "add", "inputs": [{"param": "x"}, {"ref": "ten"}]},
//       {"id": "result", "type": "mul", "inputs": [{"ref": "sum"}, {"ref": "ten"}]}
//     ]
//   }
//
// Every node has a unique "id" and a "type". Every input reference is
// either `{"ref": "<node id>"}` (another node's single output) or
// `{"param": "<name>"}` (one of the generated function's own
// parameters) - there is no third, inline-literal form: a constant is
// always its own literal_* node, matching how a real node-graph editor
// represents one (a "Number: 10" node you drag in), not a hidden magic
// value.
//
// Node types (v1 - see the session plan for what's deliberately NOT in
// v1: loops, node-defined structs, sub-graph composition):
//   literal_i64/literal_f64/literal_bool/literal_string - "value", no inputs
//   add/sub/mul/div/mod       - 2 inputs, numeric (matches Frust's own
//                               operators, including its automatic
//                               int/float promotion - see Codegen.h's
//                               compileBinary)
//   eq/neq/lt/gt/le/ge        - 2 inputs -> bool
//   if                        - 3 inputs: [cond, then, else]
//   call                      - N inputs (args) + "function" (name),
//                               "paramTypes" (array of type names),
//                               "returnType". Calls an EXTERN C-ABI
//                               function (host-registered via
//                               frust_plugin_register_host_function, or
//                               process-exported) - the compiler emits
//                               the needed `extern fn` declaration
//                               itself, once per unique function name.
//                               Not for calling other Frust-source
//                               functions (frust_plugin_host is
//                               single-file only - see its README).
//   print                     - 1 input + "valueType" - sugar for
//                               calling frust_print_i64/f64/bool/str
//                               (the same runtime exports console_io.fr
//                               wraps), auto-emitting their externs.
//
// Each node becomes one `let <id>: <type> = <expr>;` binding, in
// topologically-sorted order (a real DAG, not just a tree - a node with
// multiple consumers is bound ONCE and referenced by name from each
// consumer, not re-evaluated per use - required for correctness once a
// node can have a side effect, e.g. `print`, and desirable even for
// pure nodes to avoid duplicated generated code). A cycle in the input
// references is a real compile error, not silently ignored.

#pragma once

#include <stdint.h>

#ifdef __cplusplus

#include <optional>
#include <string>

namespace node_compiler {

struct CompileResult {
    bool ok = false;
    std::string source;       // valid only if ok
    std::string errorMessage; // valid only if !ok
};

// Parses graphJson, topologically sorts the nodes, emits .frust source,
// then VALIDATES that source actually parses and compiles (via
// frust_lang's real Lexer/Parser/Codegen - the same pipeline
// frust_compiler/frust_plugin_host use) before returning success. A
// syntactically-valid-looking graph that produces source with a real
// type error comes back as ok=false with that error, not silently
// handed to the caller as if it were fine.
CompileResult CompileGraphToSource(const std::string& graphJson);

} // namespace node_compiler

extern "C" {
#endif

#if defined(_WIN32)
#define NODE_COMPILER_API __declspec(dllexport)
#else
#define NODE_COMPILER_API __attribute__((visibility("default")))
#endif

// Returns a heap-allocated, null-terminated .frust source string on
// success, NULL on failure (call node_compiler_last_error() for why).
// Free the returned string with node_compiler_free_string().
NODE_COMPILER_API char* node_compiler_compile(const char* graphJson);
NODE_COMPILER_API void node_compiler_free_string(char* s);
// Valid only immediately after a node_compiler_compile() call that
// returned NULL - not thread-safe/reentrant beyond that single-caller
// use, same "process-wide, not a real session concept" scope as
// frust_plugin_host's own error reporting.
NODE_COMPILER_API const char* node_compiler_last_error();

#ifdef __cplusplus
}
#endif
