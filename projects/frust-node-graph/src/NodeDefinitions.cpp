#include <frust_node_graph/NodeDefinitions.h>

#include <algorithm>

namespace frust_node_graph
{
juce::String flowName(PinFlow flow)
{
    switch (flow)
    {
        case PinFlow::Data: return "data";
        case PinFlow::Exec: return "exec";
        case PinFlow::Stream: return "stream";
        case PinFlow::Resource: return "resource";
    }
    return "data";
}

PinFlow flowFromName(const juce::String& name)
{
    if (name == "exec") return PinFlow::Exec;
    if (name == "stream") return PinFlow::Stream;
    if (name == "resource") return PinFlow::Resource;
    return PinFlow::Data;
}

const PinDefinition* NodeDefinition::findInput(const juce::String& name) const
{
    for (const auto& pin : inputs)
        if (pin.name == name) return &pin;
    return nullptr;
}

const PinDefinition* NodeDefinition::findOutput(const juce::String& name) const
{
    for (const auto& pin : outputs)
        if (pin.name == name) return &pin;
    return nullptr;
}

int NodeDefinition::inputIndex(const juce::String& name) const
{
    for (int i = 0; i < (int)inputs.size(); ++i)
        if (inputs[(size_t)i].name == name) return i;
    if (variadicInputs && name.startsWith("arg") && name.substring(3).containsOnly("0123456789") && name.length() > 3)
        return name.substring(3).getIntValue() - 1;
    return -1;
}

int NodeDefinition::outputIndex(const juce::String& name) const
{
    for (int i = 0; i < (int)outputs.size(); ++i)
        if (outputs[(size_t)i].name == name) return i;
    return -1;
}

juce::String NodeDefinition::inputName(int index) const
{
    if (index >= 0 && index < (int)inputs.size()) return inputs[(size_t)index].name;
    if (variadicInputs && index >= 0) return "arg" + juce::String(index + 1);
    return {};
}

bool pinsCompatible(const PinDefinition& from, const PinDefinition& to)
{
    if (from.flow != to.flow) return false;
    return from.type == to.type || from.type == "any" || to.type == "any";
}

namespace
{
PinDefinition in(juce::String name, juce::String type = "i64") { return { name, type, PinFlow::Data }; }
PinDefinition out(juce::String name, juce::String type = "i64") { return { name, type, PinFlow::Data }; }
PinDefinition execIn(juce::String name = "in") { return { name, "exec", PinFlow::Exec }; }
PinDefinition execOut(juce::String name = "then") { return { name, "exec", PinFlow::Exec }; }
PinDefinition streamPin(juce::String name, juce::String type) { return { name, type, PinFlow::Stream }; }
PinDefinition resourcePin(juce::String name, juce::String type) { return { name, type, PinFlow::Resource }; }

NodeDefinition def(juce::String type, juce::String title, juce::String category,
                   std::vector<PinDefinition> inputs, std::vector<PinDefinition> outputs,
                   std::vector<ParameterDefinition> parameters = {})
{
    NodeDefinition d;
    d.type = type;
    d.title = title;
    d.category = category;
    d.inputs = std::move(inputs);
    d.outputs = std::move(outputs);
    d.parameters = std::move(parameters);
    return d;
}

std::vector<NodeDefinition> makeBuiltins()
{
    const std::vector<ParameterDefinition> stateParams {
        { "text", "string" }, { "accessibility", "string" }, { "initial", "bool" }, { "terminal", "bool" },
        { "entryAction", "string" }, { "updateAction", "string" }, { "exitAction", "string" }
    };
    std::vector<NodeDefinition> d {
        def("param_i64", "Parameter", "Sources", {}, { out("x") }),
        def("literal_i64", "Integer", "Sources", {}, { out("value") }, { { "value", "i64" } }),
        def("literal_string", "String", "Sources", {}, { out("value", "string") }, { { "text", "string" } }),
        def("const_bool", "Boolean", "Sources", {}, { out("value", "bool") }, { { "value", "bool" } }),
        def("add", "Add", "Math", { in("a"), in("b") }, { out("sum") }),
        def("sub", "Subtract", "Math", { in("a"), in("b") }, { out("difference") }),
        def("mul", "Multiply", "Math", { in("a"), in("b") }, { out("product") }),
        def("div", "Divide", "Math", { in("a"), in("b") }, { out("quotient") }),
        def("mod", "Modulo", "Math", { in("a"), in("b") }, { out("remainder") }),
        def("eq", "Equals", "Logic", { in("a"), in("b") }, { out("is equal", "bool") }),
        def("lt", "Less Than", "Logic", { in("a"), in("b") }, { out("is less", "bool") }),
        def("gt", "Greater Than", "Logic", { in("a"), in("b") }, { out("is greater", "bool") }),
        def("if", "If Select", "Logic", { in("cond", "bool"), in("then"), in("else") }, { out("value") }),
        def("event_start", "Event Start", "Execution", {}, { execOut("start") }),
        def("print", "Print", "Execution", { execIn(), in("value", "any") }, { execOut("then") }),
        def("sequence", "Sequence", "Execution", { execIn() }, { execOut("A"), execOut("B") }),
        def("branch", "Branch", "Execution", { execIn(), in("condition", "bool") }, { execOut("true"), execOut("false") }),
        def("while_loop", "While Loop", "Execution", { execIn(), in("condition", "bool") }, { execOut("body"), execOut("done") }),
        def("for_loop", "For Loop", "Execution", { execIn(), in("first"), in("last") }, { execOut("body"), execOut("done"), out("index") }),
        def("end", "End", "Execution", { execIn() }, {}),
        def("return", "Return", "Execution", { execIn(), in("value") }, {}),
        def("get_var", "Get Variable", "Variables", {}, { out("value") }),
        def("set_var", "Set Variable", "Variables", { execIn(), in("value") }, { execOut("then") }),
        def("make_struct", "Make Struct", "Types", { in("field A", "any"), in("field B", "any") }, { out("struct", "any") }),
        def("break_struct", "Break Struct", "Types", { in("struct", "any") }, { out("field A", "any"), out("field B", "any") }),
        def("enum_value", "Enum Value", "Types", {}, { out("variant", "any") }),
        def("match_enum", "Match Enum", "Execution", { execIn(), in("variant", "any") }, { execOut("case A"), execOut("case B"), execOut("default") }),
        def("audio_buffer_in", "Audio Buffer In", "Streams", {}, { streamPin("buffer", "AudioBuffer") }),
        def("process_audio_block", "Process Audio Block", "Streams", { streamPin("in", "AudioBuffer") }, { streamPin("out", "AudioBuffer") }),
        def("audio_buffer_out", "Audio Buffer Out", "Streams", { streamPin("buffer", "AudioBuffer") }, {}),
        def("texture_resource", "Texture Resource", "Resources", {}, { resourcePin("texture", "Texture") }),
        def("sample_texture", "Sample Texture", "Resources", { resourcePin("texture", "Texture"), in("uv", "any") }, { out("color", "any") }),
        def("reroute", "Reroute", "Graph", { in("in", "any") }, { out("out", "any") }, { { "pinType", "string" }, { "pinFlow", "string" } }),
        def("state_machine_instance", "State Machine", "State Machine",
            { execIn("tick"), in("event", "string"), in("payload", "any") },
            { execOut("then"), out("state", "string"), out("transition", "string") },
            { { "text", "string" }, { "machineRef", "string" } }),
        def("sm_state", "State", "State Machine", { execIn("enter") }, { execOut("leave") }, stateParams),
        def("sm_transition", "Transition", "State Machine", { execIn("from") }, { execOut("to") },
            { { "text", "string" }, { "event", "string", true }, { "guard", "string" }, { "action", "string" } }),
        def("sm_event", "Event", "State Machine", {}, { out("event", "string") },
            { { "text", "string" }, { "event", "string" }, { "payloadType", "string" } }),

        // node_compiler v1 types: the editor can show and save them, but does
        // not offer them in its palette.
        def("literal_f64", "Float", "Sources", {}, { out("value", "f64") }, { { "value", "f64" } }),
        def("literal_bool", "Bool Literal", "Sources", {}, { out("value", "bool") }, { { "value", "bool" } }),
        def("neq", "Not Equal", "Logic", { in("a"), in("b") }, { out("is not equal", "bool") }),
        def("le", "Less or Equal", "Logic", { in("a"), in("b") }, { out("is less or equal", "bool") }),
        def("ge", "Greater or Equal", "Logic", { in("a"), in("b") }, { out("is greater or equal", "bool") }),
        def("call", "Extern Call", "Functions", {}, { out("result", "any") },
            { { "function", "string", true }, { "returnType", "string" }, { "paramTypes", "array" } }),
    };
    for (auto& n : d)
    {
        if (n.type == "literal_i64") n.defaultLiteral = 10;
        if (n.type == "sm_state") n.multiDriveExecInputs = true;
        if (n.type == "call") n.variadicInputs = true;
        if (n.type == "literal_f64" || n.type == "literal_bool" || n.type == "neq" || n.type == "le" || n.type == "ge" || n.type == "call")
            n.inPalette = false;
    }
    return d;
}
}

const std::vector<NodeDefinition>& builtinDefinitions()
{
    static const std::vector<NodeDefinition> builtins = makeBuiltins();
    return builtins;
}

const NodeDefinition* schemaV1Definition(const juce::String& type)
{
    static const NodeDefinition v1Print = [] {
        auto d = def("print", "Print", "Execution", { in("value", "any") }, {}, { { "valueType", "string" } });
        d.inPalette = false;
        return d;
    }();
    return type == "print" ? &v1Print : nullptr;
}

NodeDefinition callNodeDefinition(const FunctionSignature& fn)
{
    NodeDefinition d;
    d.type = "call_function:" + fn.id;
    d.title = fn.namespaceName.isNotEmpty() ? fn.namespaceName + "." + fn.name : fn.name;
    d.category = "Functions";
    d.functionRef = fn.id;
    d.parameters = { { "function", "object", true } };
    if (fn.kind == "callable")
        d.inputs.push_back(execIn("in"));
    for (const auto& p : fn.inputs)
        d.inputs.push_back({ p.name, p.type, PinFlow::Data });
    if (fn.kind == "callable")
        d.outputs.push_back(execOut("then"));
    for (const auto& p : fn.outputs)
        d.outputs.push_back({ p.name, p.type, PinFlow::Data });
    return d;
}

DefinitionRegistry::DefinitionRegistry()
    : definitions(builtinDefinitions())
{
}

const NodeDefinition* DefinitionRegistry::find(const juce::String& type) const
{
    auto it = std::find_if(definitions.begin(), definitions.end(), [&type](const NodeDefinition& d) { return d.type == type; });
    return it == definitions.end() ? nullptr : &*it;
}

void DefinitionRegistry::add(NodeDefinition definition)
{
    auto it = std::find_if(definitions.begin(), definitions.end(), [&definition](const NodeDefinition& d) { return d.type == definition.type; });
    if (it != definitions.end())
        *it = std::move(definition);
    else
        definitions.push_back(std::move(definition));
}
}
