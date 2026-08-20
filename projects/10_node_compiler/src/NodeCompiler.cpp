#include "node_compiler/NodeCompiler.h"

#include <juce_core/juce_core.h>

#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "AST.h"
#include "Codegen.h"
#include "Lexer.h"
#include "ModuleLoader.h"
#include "parser.hpp"

using namespace frust;

namespace node_compiler {

namespace {

struct InputRef {
    bool isParam = false;
    std::string name; // param name or node id
};

struct NodeDef {
    std::string id;
    std::string type;
    juce::var raw;
    std::vector<InputRef> inputs;
};

struct ParamDef {
    std::string name;
    std::string type;
};

bool ParseInputRef(const juce::var& v, InputRef& out, std::string& err) {
    if (!v.isObject()) { err = "input ref must be an object"; return false; }
    auto* obj = v.getDynamicObject();
    if (obj->hasProperty("ref")) {
        out.isParam = false;
        out.name = obj->getProperty("ref").toString().toStdString();
        return true;
    }
    if (obj->hasProperty("param")) {
        out.isParam = true;
        out.name = obj->getProperty("param").toString().toStdString();
        return true;
    }
    err = "input ref must have 'ref' or 'param'";
    return false;
}

// Kahn's algorithm - real cycle detection, not silently ignored.
bool TopoSort(const std::vector<NodeDef>& nodes, std::vector<std::string>& order, std::string& err) {
    std::set<std::string> ids;
    for (auto& n : nodes) ids.insert(n.id);

    std::map<std::string, std::vector<std::string>> dependents;
    std::map<std::string, int> inDegree;
    for (auto& n : nodes) inDegree[n.id] = 0;

    for (auto& n : nodes) {
        for (auto& in : n.inputs) {
            if (in.isParam) continue;
            if (!ids.count(in.name)) {
                err = "node '" + n.id + "' references unknown node '" + in.name + "'";
                return false;
            }
            dependents[in.name].push_back(n.id);
            inDegree[n.id]++;
        }
    }

    std::vector<std::string> ready;
    for (auto& n : nodes) if (inDegree[n.id] == 0) ready.push_back(n.id);

    while (!ready.empty()) {
        std::string id = ready.back();
        ready.pop_back();
        order.push_back(id);
        for (auto& dep : dependents[id]) {
            if (--inDegree[dep] == 0) ready.push_back(dep);
        }
    }

    if (order.size() != nodes.size()) {
        err = "graph has a cycle in its node references";
        return false;
    }
    return true;
}

bool IsNumericType(const std::string& t) { return t == "i64" || t == "f64"; }

std::string FormatFnFor(const std::string& t) {
    if (t == "i64") return "frust_format_i64";
    if (t == "f64") return "frust_format_f64";
    if (t == "bool") return "frust_format_bool";
    return "";
}

struct CodegenState {
    std::map<std::string, NodeDef*> nodesById;
    std::map<std::string, std::string> paramType;  // param name -> type
    std::map<std::string, std::string> resultType; // node id -> its result Frust type
    std::set<std::string> externDeclared;
    std::ostringstream externs;
    std::ostringstream body;
    std::string* err;
};

std::string RefName(const InputRef& in) { return in.name; }

std::string RefType(CodegenState& st, const InputRef& in) {
    if (in.isParam) return st.paramType.count(in.name) ? st.paramType[in.name] : "";
    return st.resultType.count(in.name) ? st.resultType[in.name] : "";
}

void DeclareExtern(CodegenState& st, const std::string& decl, const std::string& key) {
    if (st.externDeclared.count(key)) return;
    st.externDeclared.insert(key);
    st.externs << decl << "\n";
}

// Emits `let <id>: <type> = <expr>;` and records the node's result type.
// Returns false (setting *st.err) on a real graph error.
bool EmitNode(CodegenState& st, const NodeDef& n) {
    const std::string& type = n.type;

    auto binaryArith = [&](const char* op) -> bool {
        if (n.inputs.size() != 2) { *st.err = "node '" + n.id + "' (" + type + ") needs exactly 2 inputs"; return false; }
        std::string lt = RefType(st, n.inputs[0]);
        std::string rt = RefType(st, n.inputs[1]);
        if (lt.empty() || rt.empty()) { *st.err = "node '" + n.id + "': input type unknown (bad ref?)"; return false; }
        if (!IsNumericType(lt) || !IsNumericType(rt)) { *st.err = "node '" + n.id + "': " + type + " needs numeric inputs"; return false; }
        std::string resultType = (lt == "f64" || rt == "f64") ? "f64" : "i64";
        st.body << "    let " << n.id << ": " << resultType << " = " << RefName(n.inputs[0]) << " " << op << " " << RefName(n.inputs[1]) << ";\n";
        st.resultType[n.id] = resultType;
        return true;
    };

    auto comparison = [&](const char* op) -> bool {
        if (n.inputs.size() != 2) { *st.err = "node '" + n.id + "' (" + type + ") needs exactly 2 inputs"; return false; }
        std::string lt = RefType(st, n.inputs[0]);
        std::string rt = RefType(st, n.inputs[1]);
        if (lt.empty() || rt.empty()) { *st.err = "node '" + n.id + "': input type unknown (bad ref?)"; return false; }
        st.body << "    let " << n.id << ": bool = " << RefName(n.inputs[0]) << " " << op << " " << RefName(n.inputs[1]) << ";\n";
        st.resultType[n.id] = "bool";
        return true;
    };

    if (type == "literal_i64" || type == "literal_f64" || type == "literal_bool" || type == "literal_string") {
        if (!n.raw.getDynamicObject()->hasProperty("value")) { *st.err = "literal node '" + n.id + "' needs a 'value'"; return false; }
        juce::var val = n.raw.getDynamicObject()->getProperty("value");
        std::string frustType, literalText;
        if (type == "literal_i64") { frustType = "i64"; literalText = std::to_string((int64_t)val); }
        else if (type == "literal_f64") { frustType = "f64"; literalText = std::to_string((double)val); }
        else if (type == "literal_bool") { frustType = "bool"; literalText = (bool)val ? "true" : "false"; }
        else {
            frustType = "String";
            juce::String s = val.toString();
            // Escape embedded quotes/backslashes so the generated
            // literal round-trips correctly (confirmed \" already works
            // - see Codegen.h's test coverage).
            juce::String escaped = s.replace("\\", "\\\\").replace("\"", "\\\"");
            literalText = "\"" + escaped.toStdString() + "\"";
        }
        st.body << "    let " << n.id << ": " << frustType << " = " << literalText << ";\n";
        st.resultType[n.id] = frustType;
        return true;
    }
    if (type == "add") return binaryArith("+");
    if (type == "sub") return binaryArith("-");
    if (type == "mul") return binaryArith("*");
    if (type == "div") return binaryArith("/");
    if (type == "mod") return binaryArith("%");
    if (type == "eq")  return comparison("==");
    if (type == "neq") return comparison("!=");
    if (type == "lt")  return comparison("<");
    if (type == "gt")  return comparison(">");
    if (type == "le")  return comparison("<=");
    if (type == "ge")  return comparison(">=");

    if (type == "if") {
        if (n.inputs.size() != 3) { *st.err = "node '" + n.id + "' (if) needs exactly 3 inputs: [cond, then, else]"; return false; }
        std::string condType = RefType(st, n.inputs[0]);
        if (condType != "bool") { *st.err = "node '" + n.id + "': if's condition input must be bool"; return false; }
        std::string thenType = RefType(st, n.inputs[1]);
        std::string elseType = RefType(st, n.inputs[2]);
        if (thenType.empty() || elseType.empty()) { *st.err = "node '" + n.id + "': then/else input type unknown"; return false; }
        if (thenType != elseType) { *st.err = "node '" + n.id + "': if's then/else branches have different types (" + thenType + " vs " + elseType + ")"; return false; }
        st.body << "    let " << n.id << ": " << thenType << " = if (" << RefName(n.inputs[0]) << ") { "
                << RefName(n.inputs[1]) << " } else { " << RefName(n.inputs[2]) << " };\n";
        st.resultType[n.id] = thenType;
        return true;
    }

    if (type == "call") {
        auto* obj = n.raw.getDynamicObject();
        if (!obj->hasProperty("function") || !obj->hasProperty("returnType")) {
            *st.err = "node '" + n.id + "' (call) needs 'function' and 'returnType'";
            return false;
        }
        std::string fnName = obj->getProperty("function").toString().toStdString();
        std::string retType = obj->getProperty("returnType").toString().toStdString();
        std::vector<std::string> paramTypes;
        if (obj->hasProperty("paramTypes") && obj->getProperty("paramTypes").isArray()) {
            for (auto& pt : *obj->getProperty("paramTypes").getArray()) paramTypes.push_back(pt.toString().toStdString());
        }
        if (paramTypes.size() != n.inputs.size()) {
            *st.err = "node '" + n.id + "': " + std::to_string(n.inputs.size()) + " input(s) but " + std::to_string(paramTypes.size()) + " paramTypes";
            return false;
        }
        std::ostringstream sig;
        sig << "extern fn " << fnName << "(";
        for (size_t i = 0; i < paramTypes.size(); ++i) { if (i) sig << ", "; sig << "a" << i << ": " << paramTypes[i]; }
        sig << ") -> " << retType << ";";
        DeclareExtern(st, sig.str(), "fn:" + fnName);

        st.body << "    let " << n.id << ": " << retType << " = " << fnName << "(";
        for (size_t i = 0; i < n.inputs.size(); ++i) { if (i) st.body << ", "; st.body << RefName(n.inputs[i]); }
        st.body << ");\n";
        st.resultType[n.id] = retType;
        return true;
    }

    if (type == "print") {
        auto* obj = n.raw.getDynamicObject();
        if (!obj->hasProperty("valueType")) { *st.err = "node '" + n.id + "' (print) needs 'valueType'"; return false; }
        if (n.inputs.size() != 1) { *st.err = "node '" + n.id + "' (print) needs exactly 1 input"; return false; }
        std::string valueType = obj->getProperty("valueType").toString().toStdString();
        DeclareExtern(st, "extern fn frust_print_str(val: String);", "frust_print_str");
        if (valueType == "string") {
            st.body << "    frust_print_str(" << RefName(n.inputs[0]) << ");\n";
        } else {
            std::string fmtFn = FormatFnFor(valueType);
            if (fmtFn.empty()) { *st.err = "node '" + n.id + "': unknown print valueType '" + valueType + "'"; return false; }
            std::string retT = (valueType == "bool") ? "bool" : valueType;
            DeclareExtern(st, "extern fn " + fmtFn + "(val: " + retT + ") -> String;", fmtFn);
            st.body << "    frust_print_str(" << fmtFn << "(" << RefName(n.inputs[0]) << "));\n";
        }
        // print has no value - not recorded in resultType, so it can
        // never be referenced by a downstream node or chosen as the
        // graph's "output" (checked explicitly below).
        return true;
    }

    *st.err = "unknown node type '" + type + "' (node '" + n.id + "')";
    return false;
}

// Parses the source through frust_lang's real pipeline (the same
// Lexer/Parser/Codegen frust_compiler and frust_plugin_host use) to
// confirm it actually compiles - not just "looks like valid text."
bool ValidateCompiles(const std::string& source, std::string& err) {
    std::istringstream input(source);
    Lexer lexer(&input);
    Program* result = nullptr;
    std::vector<std::string> parseErrors;
    std::vector<ParseError> structuredErrors;
    // Intentionally leaked (process-lifetime validation call, matches
    // frust_plugin_host's own load-path pattern) - the SAME arena
    // instance is reused for both the parse and ResolveImports below,
    // since AST nodes allocated during parsing live in it.
    AstArena* arena = new AstArena();
    Parser parser(lexer, *arena, parseErrors, result, structuredErrors);
    parser.parse();
    parseErrors.insert(parseErrors.end(), lexer.errors.begin(), lexer.errors.end());
    if (result) ResolveImports(result, *arena, parseErrors);

    if (!parseErrors.empty() || !result) {
        std::ostringstream msg;
        for (auto& e : parseErrors) msg << e << "\n";
        err = msg.str();
        return false;
    }

    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("node_compiler_validate", *context);
    Codegen codegen(*context, *module);
    if (!codegen.compileProgram(*result)) {
        err = "generated source failed codegen (see stderr for details)";
        return false;
    }
    return true;
}

} // namespace

CompileResult CompileGraphToSource(const std::string& graphJson) {
    CompileResult result;

    juce::var parsed;
    auto parseResult = juce::JSON::parse(juce::String(graphJson), parsed);
    if (parseResult.failed() || !parsed.isObject()) {
        result.errorMessage = "invalid graph JSON: " + parseResult.getErrorMessage().toStdString();
        return result;
    }
    auto* root = parsed.getDynamicObject();

    if (!root->hasProperty("functionName") || !root->hasProperty("output") || !root->hasProperty("nodes")) {
        result.errorMessage = "graph JSON needs 'functionName', 'output', and 'nodes'";
        return result;
    }
    std::string functionName = root->getProperty("functionName").toString().toStdString();
    std::string outputId = root->getProperty("output").toString().toStdString();

    std::vector<ParamDef> params;
    if (root->hasProperty("params") && root->getProperty("params").isArray()) {
        for (auto& p : *root->getProperty("params").getArray()) {
            if (!p.isObject()) { result.errorMessage = "each param must be an object"; return result; }
            auto* pObj = p.getDynamicObject();
            ParamDef pd;
            pd.name = pObj->getProperty("name").toString().toStdString();
            pd.type = pObj->getProperty("type").toString().toStdString();
            params.push_back(pd);
        }
    }

    if (!parsed.getDynamicObject()->getProperty("nodes").isArray()) {
        result.errorMessage = "'nodes' must be an array";
        return result;
    }

    std::vector<NodeDef> nodes;
    std::set<std::string> seenIds;
    for (auto& nv : *root->getProperty("nodes").getArray()) {
        if (!nv.isObject()) { result.errorMessage = "each node must be an object"; return result; }
        auto* nObj = nv.getDynamicObject();
        NodeDef n;
        n.id = nObj->getProperty("id").toString().toStdString();
        n.type = nObj->getProperty("type").toString().toStdString();
        n.raw = nv;
        if (n.id.empty()) { result.errorMessage = "node missing 'id'"; return result; }
        if (!seenIds.insert(n.id).second) { result.errorMessage = "duplicate node id '" + n.id + "'"; return result; }
        if (nObj->hasProperty("inputs") && nObj->getProperty("inputs").isArray()) {
            for (auto& iv : *nObj->getProperty("inputs").getArray()) {
                InputRef ref;
                std::string err;
                if (!ParseInputRef(iv, ref, err)) { result.errorMessage = "node '" + n.id + "': " + err; return result; }
                n.inputs.push_back(ref);
            }
        }
        nodes.push_back(std::move(n));
    }

    std::vector<std::string> order;
    std::string topoErr;
    if (!TopoSort(nodes, order, topoErr)) {
        result.errorMessage = topoErr;
        return result;
    }

    CodegenState st;
    std::string genErr;
    st.err = &genErr;
    for (auto& p : params) st.paramType[p.name] = p.type;
    std::map<std::string, NodeDef*> byId;
    for (auto& n : nodes) byId[n.id] = &n;

    for (auto& id : order) {
        if (!EmitNode(st, *byId[id])) {
            result.errorMessage = genErr;
            return result;
        }
    }

    if (!st.resultType.count(outputId)) {
        result.errorMessage = "output node '" + outputId + "' does not exist or produces no value (e.g. a 'print' node can't be the output)";
        return result;
    }
    std::string returnType = st.resultType[outputId];

    std::ostringstream src;
    src << st.externs.str();
    if (!st.externs.str().empty()) src << "\n";
    src << "pub fn " << functionName << "(";
    for (size_t i = 0; i < params.size(); ++i) { if (i) src << ", "; src << params[i].name << ": " << params[i].type; }
    src << ") -> " << returnType << " = {\n";
    src << st.body.str();
    src << "    " << outputId << "\n";
    src << "}\n";

    std::string validateErr;
    if (!ValidateCompiles(src.str(), validateErr)) {
        result.errorMessage = "generated source does not compile: " + validateErr + "\n--- generated source ---\n" + src.str();
        return result;
    }

    result.ok = true;
    result.source = src.str();
    return result;
}

} // namespace node_compiler

namespace {
thread_local std::string g_lastError;
} // namespace

extern "C" {

NODE_COMPILER_API char* node_compiler_compile(const char* graphJson) {
    auto result = node_compiler::CompileGraphToSource(graphJson ? graphJson : "");
    if (!result.ok) {
        g_lastError = result.errorMessage;
        return nullptr;
    }
    char* out = new char[result.source.size() + 1];
    memcpy(out, result.source.c_str(), result.source.size() + 1);
    return out;
}

NODE_COMPILER_API void node_compiler_free_string(char* s) {
    delete[] s;
}

NODE_COMPILER_API const char* node_compiler_last_error() {
    return g_lastError.c_str();
}

} // extern "C"
