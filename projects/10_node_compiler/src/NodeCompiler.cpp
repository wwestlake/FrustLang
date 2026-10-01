#include "node_compiler/NodeCompiler.h"

#include <juce_core/juce_core.h>

#include <cstring>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
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
    // Was `new AstArena()`, deliberately never deleted, on the claim this
    // was a "process-lifetime validation call" - false: ValidateCompiles
    // runs once per CompileGraphToSource call, and that's the body of
    // node_compiler_compile(), the library's actual per-request public
    // API (a live graph editor calls this on every compile, not once at
    // startup) - confirmed by tracing the call chain, not just an
    // imprecise comment. Every node ID/type string parsed here also had
    // nowhere else to live, so the leak grew without bound over a long
    // editing session. AstArena only needs to outlive this function
    // (compileProgram fully consumes `result` before returning) - a
    // stack-local instance is all that was ever needed, matching how
    // frust_plugin_host's own load path actually does it (the comment's
    // claim of matching that pattern was itself wrong - that path never
    // leaked its arena either).
    AstArena arena;
    Parser parser(lexer, arena, parseErrors, result, structuredErrors);
    parser.parse();
    parseErrors.insert(parseErrors.end(), lexer.errors.begin(), lexer.errors.end());
    if (result) ResolveImports(result, arena, parseErrors);

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

namespace {

std::string JsonString(const juce::var& obj, const char* name, const std::string& fallback = {}) {
    if (!obj.isObject()) return fallback;
    auto* dyn = obj.getDynamicObject();
    if (dyn == nullptr || !dyn->hasProperty(name)) return fallback;
    return dyn->getProperty(name).toString().toStdString();
}

std::string JsonString(const juce::DynamicObject* obj, const char* name, const std::string& fallback = {}) {
    if (obj == nullptr || !obj->hasProperty(name)) return fallback;
    return obj->getProperty(name).toString().toStdString();
}

std::string SanitizeIdentifier(std::string text, const std::string& fallback) {
    std::string out;
    for (char c : text) {
        if (std::isalnum((unsigned char)c) || c == '_') out.push_back(c);
        else if (c == '-' || c == ' ' || c == '.') out.push_back('_');
    }
    if (out.empty()) out = fallback;
    if (!std::isalpha((unsigned char)out.front()) && out.front() != '_')
        out = "_" + out;
    return out;
}

std::string EscapeFrustString(const juce::String& s) {
    return ("\"" + s.replace("\\", "\\\\").replace("\"", "\\\"")).toStdString() + "\"";
}

std::string NormalizePrintValueType(const std::string& type) {
    if (type == "String") return "string";
    if (type == "bool" || type == "i64" || type == "f64" || type == "string") return type;
    return "string";
}

struct ExecValue {
    std::string expression;
    std::string type;
};

struct StateMachineState {
    std::string id;
    std::string name;
    bool initial = false;
    bool terminal = false;
};

struct StateMachineEvent {
    std::string id;
    std::string name;
};

struct StateMachineTransition {
    std::string id;
    std::string from;
    std::string to;
    std::string event;
    std::string guard;
    std::string action;
};

bool ResolveExecutableValue(const std::map<std::string, juce::var>& nodesById,
                            const juce::var& ref,
                            ExecValue& value,
                            std::string& err) {
    if (!ref.isObject()) {
        err = "print value input must be an object reference";
        return false;
    }

    auto* refObj = ref.getDynamicObject();
    const auto sourceId = JsonString(refObj, "ref");
    if (sourceId.empty()) {
        err = "print value input must reference a node";
        return false;
    }

    auto found = nodesById.find(sourceId);
    if (found == nodesById.end()) {
        err = "print value references unknown node '" + sourceId + "'";
        return false;
    }

    auto* node = found->second.getDynamicObject();
    const auto type = JsonString(node, "type");
    if (type == "literal_string") {
        value.expression = EscapeFrustString(node->getProperty("text").toString());
        value.type = "string";
        return true;
    }
    if (type == "literal_i64") {
        value.expression = std::to_string((int64_t)node->getProperty("value"));
        value.type = "i64";
        return true;
    }
    if (type == "literal_f64") {
        value.expression = std::to_string((double)node->getProperty("value"));
        value.type = "f64";
        return true;
    }
    if (type == "const_bool" || type == "literal_bool") {
        value.expression = (bool)node->getProperty("value") ? "true" : "false";
        value.type = "bool";
        return true;
    }

    err = "executable print currently supports literal string/i64/f64/bool values; node '" + sourceId
        + "' is type '" + type + "'";
    return false;
}

bool CompileExecutablePod(const juce::var& parsed, SchematicCompileResult& result) {
    auto* root = parsed.getDynamicObject();
    const auto diagramName = JsonString(root, "name", JsonString(root, "functionName", "node_program"));
    result.packageName = SanitizeIdentifier(diagramName, "node_program");
    result.namespaceName = JsonString(root, "namespace", result.packageName);
    result.artifactKind = ArtifactKind::FrustExecutablePod;
    result.entryFile = "src/main.fr";
    result.requiredPods.push_back({ "core", "1.0.1" });

    if (!root->hasProperty("nodes") || !root->getProperty("nodes").isArray()) {
        result.errorMessage = "executable schematic needs a nodes array";
        return false;
    }

    std::map<std::string, juce::var> nodesById;
    for (auto& nodeVar : *root->getProperty("nodes").getArray()) {
        if (!nodeVar.isObject()) {
            result.errorMessage = "each node must be an object";
            return false;
        }
        auto* node = nodeVar.getDynamicObject();
        const auto id = JsonString(node, "id");
        if (id.empty()) {
            result.errorMessage = "node missing id";
            return false;
        }
        nodesById[id] = nodeVar;
    }

    std::ostringstream src;
    src << "import core, \"current\";\n\n";
    src << "fn main() -> i64 = {\n";

    int line = 4;
    bool emittedAction = false;
    for (auto& nodeVar : *root->getProperty("nodes").getArray()) {
        auto* node = nodeVar.getDynamicObject();
        const auto type = JsonString(node, "type");
        if (type != "print") continue;

        auto inputs = node->getProperty("inputs");
        if (!inputs.isArray() || inputs.getArray()->size() < 1) {
            result.errorMessage = "print node '" + JsonString(node, "id") + "' needs a value input";
            return false;
        }

        // Rich execution schematics use input 0 for exec and input 1 for
        // data. Older pure graph print nodes use input 0 for the value.
        const int valueIndex = inputs.getArray()->size() > 1 ? 1 : 0;
        ExecValue value;
        std::string err;
        if (!ResolveExecutableValue(nodesById, inputs.getArray()->getReference(valueIndex), value, err)) {
            result.errorMessage = "print node '" + JsonString(node, "id") + "': " + err;
            return false;
        }

        const auto valueType = NormalizePrintValueType(value.type);
        if (valueType == "string")
            src << "    println_str(" << value.expression << ");\n";
        else if (valueType == "i64")
            src << "    println_i64(" << value.expression << ");\n";
        else if (valueType == "f64")
            src << "    println_f64(" << value.expression << ");\n";
        else if (valueType == "bool")
            src << "    println_bool(" << value.expression << ");\n";
        else {
            result.errorMessage = "print node '" + JsonString(node, "id") + "' has unsupported value type '" + valueType + "'";
            return false;
        }

        result.sourceMap.push_back({ JsonString(node, "id"), result.entryFile, line++, 5 });
        emittedAction = true;
    }

    if (!emittedAction) {
        result.errorMessage = "executable schematic has no runnable action nodes yet";
        return false;
    }

    src << "    0\n";
    src << "}\n";

    result.source = src.str();
    result.files.push_back({ result.entryFile, result.source });

    std::ostringstream frate;
    frate << "{\n"
          << "  \"name\": \"" << result.packageName << "\",\n"
          << "  \"version\": \"0.1.0\",\n"
          << "  \"type\": \"bin\",\n"
          << "  \"description\": \"Generated from a node schematic. The schematic JSON is authoritative.\",\n"
          << "  \"exports\": [],\n"
          << "  \"dependencies\": [\n"
          << "    { \"name\": \"core\", \"version\": \"1.0.1\" }\n"
          << "  ]\n"
          << "}\n";
    result.frateJson = frate.str();
    result.files.push_back({ "frate.json", result.frateJson });

    std::string validateErr;
    if (!ValidateCompiles(result.source, validateErr)) {
        result.errorMessage = "generated executable source does not compile: " + validateErr
            + "\n--- generated source ---\n" + result.source;
        return false;
    }

    result.ok = true;
    return true;
}

std::string StateFnName(const std::string& id) {
    return "state_" + SanitizeIdentifier(id, "unnamed");
}

std::string EventFnName(const std::string& id) {
    return "event_" + SanitizeIdentifier(id, "unnamed");
}

bool ParseStateMachine(const juce::var& parsed,
                       std::vector<StateMachineState>& states,
                       std::vector<StateMachineEvent>& events,
                       std::vector<StateMachineTransition>& transitions,
                       std::string& initialState,
                       std::string& err) {
    auto* root = parsed.getDynamicObject();
    if (root == nullptr) {
        err = "state-machine schematic root must be an object";
        return false;
    }

    auto statesVar = root->getProperty("states");
    if (!statesVar.isArray()) {
        err = "state-machine schematic needs a states array";
        return false;
    }

    std::set<std::string> stateIds;
    for (auto& stateVar : *statesVar.getArray()) {
        if (!stateVar.isObject()) {
            err = "each state must be an object";
            return false;
        }
        auto* stateObj = stateVar.getDynamicObject();
        StateMachineState state;
        state.id = JsonString(stateObj, "id");
        state.name = JsonString(stateObj, "name", state.id);
        state.initial = (bool)stateObj->getProperty("initial");
        state.terminal = (bool)stateObj->getProperty("terminal");
        if (state.id.empty()) {
            err = "state missing id";
            return false;
        }
        if (!stateIds.insert(state.id).second) {
            err = "duplicate state id '" + state.id + "'";
            return false;
        }
        if (state.initial) {
            if (!initialState.empty()) {
                err = "state machine can only have one initial state";
                return false;
            }
            initialState = state.id;
        }
        states.push_back(std::move(state));
    }

    if (states.empty()) {
        err = "state machine needs at least one state";
        return false;
    }
    if (initialState.empty()) {
        err = "state machine needs one initial state";
        return false;
    }

    auto eventsVar = root->getProperty("events");
    std::set<std::string> eventIds;
    if (eventsVar.isArray()) {
        for (auto& eventVar : *eventsVar.getArray()) {
            if (!eventVar.isObject()) {
                err = "each event must be an object";
                return false;
            }
            auto* eventObj = eventVar.getDynamicObject();
            StateMachineEvent event;
            event.id = JsonString(eventObj, "id");
            event.name = JsonString(eventObj, "name", event.id);
            if (event.id.empty()) {
                err = "event missing id";
                return false;
            }
            if (eventIds.insert(event.id).second)
                events.push_back(std::move(event));
        }
    }

    auto transitionsVar = root->getProperty("transitions");
    if (transitionsVar.isArray()) {
        std::set<std::string> transitionIds;
        for (auto& transitionVar : *transitionsVar.getArray()) {
            if (!transitionVar.isObject()) {
                err = "each transition must be an object";
                return false;
            }
            auto* transitionObj = transitionVar.getDynamicObject();
            StateMachineTransition transition;
            transition.id = JsonString(transitionObj, "id");
            transition.from = JsonString(transitionObj, "from");
            transition.to = JsonString(transitionObj, "to");
            transition.event = JsonString(transitionObj, "event");
            transition.guard = JsonString(transitionObj, "guard");
            transition.action = JsonString(transitionObj, "action");
            if (transition.id.empty()) {
                err = "transition missing id";
                return false;
            }
            if (!transitionIds.insert(transition.id).second) {
                err = "duplicate transition id '" + transition.id + "'";
                return false;
            }
            if (!stateIds.count(transition.from)) {
                err = "transition '" + transition.id + "' references unknown source state '" + transition.from + "'";
                return false;
            }
            if (!stateIds.count(transition.to)) {
                err = "transition '" + transition.id + "' references unknown destination state '" + transition.to + "'";
                return false;
            }
            if (transition.event.empty()) {
                err = "transition '" + transition.id + "' needs an event";
                return false;
            }
            if (eventIds.insert(transition.event).second)
                events.push_back({ transition.event, transition.event });
            transitions.push_back(std::move(transition));
        }
    }

    return true;
}

bool CompileStateMachinePod(const juce::var& parsed, SchematicCompileResult& result) {
    auto* root = parsed.getDynamicObject();
    const auto diagramName = JsonString(root, "name", "state_machine");
    auto targetOptions = root->getProperty("targetOptions");
    auto frustOptions = targetOptions.getProperty("frust", {});
    const auto projectType = JsonString(frustOptions, "projectType", "lib");
    result.packageName = SanitizeIdentifier(diagramName, "state_machine");
    result.namespaceName = JsonString(root, "namespace", result.packageName);
    result.artifactKind = projectType == "bin" ? ArtifactKind::FrustExecutablePod : ArtifactKind::FrustLibraryPod;
    result.entryFile = projectType == "bin" ? "src/main.fr" : "src/lib.fr";
    result.requiredPods.push_back({ "core", "1.0.1" });

    std::vector<StateMachineState> states;
    std::vector<StateMachineEvent> events;
    std::vector<StateMachineTransition> transitions;
    std::string initialState;
    std::string parseErr;
    if (!ParseStateMachine(parsed, states, events, transitions, initialState, parseErr)) {
        result.errorMessage = parseErr;
        return false;
    }

    std::ostringstream src;
    src << "import core, \"current\";\n\n";
    src << "// Generated from state-machine schematic '" << diagramName << "'.\n";
    src << "// The schematic JSON remains authoritative; this is a lowerable Frust artifact.\n\n";

    for (size_t i = 0; i < states.size(); ++i) {
        src << "pub fn " << StateFnName(states[i].id) << "() -> i64 = { " << i << " }\n";
    }
    src << "\n";
    for (size_t i = 0; i < events.size(); ++i) {
        src << "pub fn " << EventFnName(events[i].id) << "() -> i64 = { " << i << " }\n";
    }
    src << "\n";

    src << "pub fn initial_state() -> i64 = {\n";
    src << "    " << StateFnName(initialState) << "()\n";
    src << "}\n\n";

    src << "pub fn state_name(state: i64) -> String = {\n";
    for (size_t i = 0; i < states.size(); ++i) {
        src << "    if (state == " << StateFnName(states[i].id) << "()) { "
            << EscapeFrustString(juce::String(states[i].name)) << " } else {\n";
    }
    src << "    \"Unknown\"\n";
    for (size_t i = 0; i < states.size(); ++i) {
        src << "    }";
        if (i + 1 < states.size()) src << "\n";
    }
    src << "\n}\n\n";

    src << "pub fn step(current_state: i64, event: i64) -> i64 = {\n";
    src << "    let next_state: i64 = current_state;\n";
    for (const auto& transition : transitions) {
        src << "    if (current_state == " << StateFnName(transition.from) << "()) {\n";
        src << "        if (event == " << EventFnName(transition.event) << "()) {\n";
        if (!transition.guard.empty())
            src << "            // guard: " << transition.guard << "\n";
        if (!transition.action.empty())
            src << "            // action: " << transition.action << "\n";
        src << "            next_state = " << StateFnName(transition.to) << "();\n";
        src << "        } else { next_state = next_state; };\n";
        src << "    } else { next_state = next_state; };\n";
    }
    src << "    next_state\n";
    src << "}\n";

    if (projectType == "bin") {
        src << "\nfn main() -> i64 = {\n";
        src << "    let state: i64 = initial_state();\n";
        src << "    println_str(\"Initial state:\");\n";
        src << "    println_str(state_name(state));\n";
        if (!transitions.empty()) {
            src << "    let after: i64 = step(state, " << EventFnName(transitions.front().event) << "());\n";
            src << "    println_str(\"After first transition event:\");\n";
            src << "    println_str(state_name(after));\n";
        }
        src << "    0\n";
        src << "}\n";
    }

    result.source = src.str();
    result.files.push_back({ result.entryFile, result.source });

    std::ostringstream frate;
    frate << "{\n"
          << "  \"name\": \"" << result.packageName << "\",\n"
          << "  \"version\": \"0.1.0\",\n"
          << "  \"type\": \"" << (projectType == "bin" ? "bin" : "lib") << "\",\n"
          << "  \"description\": \"Generated from a state-machine node schematic. The schematic JSON is authoritative.\",\n"
          << "  \"exports\": [],\n"
          << "  \"dependencies\": [\n"
          << "    { \"name\": \"core\", \"version\": \"1.0.1\" }\n"
          << "  ]\n"
          << "}\n";
    result.frateJson = frate.str();
    result.files.push_back({ "frate.json", result.frateJson });

    int line = 0;
    for (const auto& transition : transitions) {
        result.sourceMap.push_back({ transition.id, result.entryFile, ++line, 1 });
    }
    result.diagnostics.push_back("state-machine lowering emitted integer-backed states/events; enum-backed lowering is the next compatibility step");

    std::string validateErr;
    if (!ValidateCompiles(result.source, validateErr)) {
        result.errorMessage = "generated state-machine source does not compile: " + validateErr
            + "\n--- generated source ---\n" + result.source;
        return false;
    }

    result.ok = true;
    return true;
}

} // namespace

SchematicCompileResult CompileSchematic(const std::string& schematicJson) {
    SchematicCompileResult result;

    juce::var parsed;
    auto parseResult = juce::JSON::parse(juce::String(schematicJson), parsed);
    if (parseResult.failed() || !parsed.isObject()) {
        result.errorMessage = "invalid schematic JSON: " + parseResult.getErrorMessage().toStdString();
        return result;
    }

    auto* root = parsed.getDynamicObject();
    const auto diagramType = JsonString(root, "diagramType", "node_graph");
    auto targetOptions = root->getProperty("targetOptions");
    auto frustOptions = targetOptions.getProperty("frust", {});
    const auto projectType = JsonString(frustOptions, "projectType", "function");

    if (diagramType == "state_machine") {
        CompileStateMachinePod(parsed, result);
        return result;
    }

    if (projectType == "bin") {
        CompileExecutablePod(parsed, result);
        return result;
    }

    const auto functionResult = CompileGraphToSource(schematicJson);
    if (!functionResult.ok) {
        result.errorMessage = functionResult.errorMessage;
        return result;
    }

    result.ok = true;
    result.artifactKind = projectType == "lib" ? ArtifactKind::FrustLibraryPod : ArtifactKind::FunctionSource;
    result.packageName = SanitizeIdentifier(JsonString(root, "name", "node_graph"), "node_graph");
    result.namespaceName = JsonString(root, "namespace", result.packageName);
    result.entryFile = projectType == "lib" ? "src/lib.fr" : "";
    result.source = functionResult.source;
    if (result.artifactKind == ArtifactKind::FrustLibraryPod) {
        result.files.push_back({ "src/lib.fr", result.source });
        result.frateJson = "{\n"
            "  \"name\": \"" + result.packageName + "\",\n"
            "  \"version\": \"0.1.0\",\n"
            "  \"type\": \"lib\",\n"
            "  \"description\": \"Generated from a node schematic. The schematic JSON is authoritative.\",\n"
            "  \"exports\": [],\n"
            "  \"dependencies\": []\n"
            "}\n";
        result.files.push_back({ "frate.json", result.frateJson });
    }
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
