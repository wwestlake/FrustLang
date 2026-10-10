#include <frust_node_graph/NodeGraph.h>

#include <algorithm>
#include <map>
#include <set>

namespace frust_node_graph
{
namespace
{
juce::String text(const juce::var& object, const juce::Identifier& key, const juce::String& fallback = {})
{
    if (!object.isObject() || !object.hasProperty(key)) return fallback;
    const auto value = object.getProperty(key, {});
    return value.isVoid() ? fallback : value.toString();
}

bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

Position positionOf(const juce::var& object)
{
    Position p;
    if (object.isObject() && isNumber(object.getProperty("x", {})) && isNumber(object.getProperty("y", {})))
    {
        p.x = (float)(double)object.getProperty("x", 0.0);
        p.y = (float)(double)object.getProperty("y", 0.0);
        p.present = true;
    }
    return p;
}

void writePosition(juce::DynamicObject& obj, const Position& p)
{
    if (!p.present) return;
    obj.setProperty("x", (double)p.x);
    obj.setProperty("y", (double)p.y);
}

void report(std::vector<Diagnostic>& out, Diagnostic::Severity severity, const juce::String& code, const juce::String& message,
            const juce::String& nodeId = {}, const juce::String& connectionId = {})
{
    out.push_back({ severity, code, message, nodeId, connectionId });
}

void error(std::vector<Diagnostic>& out, const juce::String& code, const juce::String& message,
           const juce::String& nodeId = {}, const juce::String& connectionId = {})
{
    report(out, Diagnostic::Severity::Error, code, message, nodeId, connectionId);
}

void warning(std::vector<Diagnostic>& out, const juce::String& code, const juce::String& message,
             const juce::String& nodeId = {}, const juce::String& connectionId = {})
{
    report(out, Diagnostic::Severity::Warning, code, message, nodeId, connectionId);
}

std::vector<PinDefinition> portsOf(const juce::var& list, const juce::String& defaultName, const juce::String& defaultType)
{
    std::vector<PinDefinition> ports;
    if (auto* arr = list.getArray())
        for (const auto& p : *arr)
            ports.push_back({ text(p, "name", defaultName), text(p, "type", defaultType), PinFlow::Data });
    return ports;
}

juce::var portsVar(const std::vector<PinDefinition>& ports)
{
    juce::Array<juce::var> arr;
    for (const auto& p : ports)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("name", p.name);
        o->setProperty("type", p.type);
        arr.add(juce::var(o));
    }
    return arr;
}

juce::String endpointText(const Endpoint& e)
{
    return e.node + "." + (e.pin.isNotEmpty() ? e.pin : juce::String(e.index));
}

juce::String connectionIdFor(const Connection& c)
{
    return endpointText(c.from) + "->" + endpointText(c.to);
}

// Gives every connection without an id a stable one derived from its ends.
void assignConnectionIds(std::vector<Connection>& connections)
{
    std::set<juce::String> used;
    for (const auto& c : connections)
        if (c.id.isNotEmpty()) used.insert(c.id);
    for (auto& c : connections)
    {
        if (c.id.isNotEmpty()) continue;
        auto base = connectionIdFor(c);
        auto id = base;
        for (int n = 2; used.count(id) != 0; ++n)
            id = base + "#" + juce::String(n);
        c.id = id;
        used.insert(id);
    }
}

// Fills in whichever of pin name and index an endpoint lacks.
void resolveEndpoint(Endpoint& e, bool isInputSide, const Graph& graph, const DefinitionRegistry& registry)
{
    if (e.graphInput)
    {
        if (e.pin.isEmpty()) e.pin = e.node;
        if (e.index < 0) e.index = 0;
        return;
    }
    const auto* node = graph.findNode(e.node);
    NodeDefinition d;
    if (node == nullptr || !graph.resolveDefinition(*node, registry, d)) return;
    if (isInputSide)
    {
        if (e.pin.isEmpty() && e.index >= 0) e.pin = d.inputName(e.index);
        else if (e.pin.isNotEmpty() && e.index < 0) e.index = d.inputIndex(e.pin);
    }
    else
    {
        if (e.pin.isEmpty() && e.index >= 0 && e.index < (int)d.outputs.size()) e.pin = d.outputs[(size_t)e.index].name;
        else if (e.pin.isNotEmpty() && e.index < 0) e.index = d.outputIndex(e.pin);
    }
}

bool parseEndpoint(const juce::var& v, bool allowGraphInput, Endpoint& out)
{
    if (!v.isObject()) return false;
    if (allowGraphInput && v.hasProperty("input"))
    {
        out.node = text(v, "input");
        out.graphInput = true;
    }
    else
        out.node = text(v, "node");
    out.pin = text(v, "pin");
    out.index = isNumber(v.getProperty("index", {})) ? (int)v.getProperty("index", -1) : -1;
    return out.node.isNotEmpty() && (out.pin.isNotEmpty() || out.index >= 0);
}

bool usesSchemaV1Shapes(const Graph& graph)
{
    if (graph.schemaVersion != 1) return false;
    return std::any_of(graph.nodes.begin(), graph.nodes.end(), [](const Node& n) { return schemaV1Definition(n.type) != nullptr; });
}

const juce::StringArray nodeStructuralKeys { "id", "type", "x", "y", "inputs" };
}

juce::String Node::functionRef() const
{
    const auto fn = parameters["function"];
    if (fn.isObject())
        return text(fn, "id", text(fn, "name"));
    if (type.startsWith("call_function:"))
        return type.fromFirstOccurrenceOf(":", false, false);
    return parameters["functionRef"].toString();
}

juce::String describe(const Diagnostic& d)
{
    juce::String s = d.isError() ? "error" : "warning";
    s << " [" << d.code << "] " << d.message;
    return s;
}

Node* Graph::findNode(const juce::String& id)
{
    auto it = std::find_if(nodes.begin(), nodes.end(), [&id](const Node& n) { return n.id == id; });
    return it == nodes.end() ? nullptr : &*it;
}

const Node* Graph::findNode(const juce::String& id) const
{
    auto it = std::find_if(nodes.begin(), nodes.end(), [&id](const Node& n) { return n.id == id; });
    return it == nodes.end() ? nullptr : &*it;
}

const InterfacePort* Graph::findInput(const juce::String& inputName) const
{
    auto it = std::find_if(interface.inputs.begin(), interface.inputs.end(), [&inputName](const InterfacePort& p) { return p.name == inputName; });
    return it == interface.inputs.end() ? nullptr : &*it;
}

const GraphReference* Graph::findSubgraph(const juce::String& id) const
{
    auto it = std::find_if(subgraphs.begin(), subgraphs.end(), [&id](const GraphReference& r) {
        return r.signature.id == id || r.signature.name == id;
    });
    return it == subgraphs.end() ? nullptr : &*it;
}

juce::String Graph::uniqueNodeId(const juce::String& base) const
{
    auto clean = base.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_").toLowerCase();
    if (clean.isEmpty()) clean = "node";
    auto candidate = clean;
    for (int suffix = 2; findNode(candidate) != nullptr || findInput(candidate) != nullptr; ++suffix)
        candidate = clean + "_" + juce::String(suffix);
    return candidate;
}

Node& Graph::addNode(const juce::String& type, const juce::String& idBase)
{
    Node n;
    n.type = type;
    n.id = uniqueNodeId(idBase.isNotEmpty() ? idBase : type);
    nodes.push_back(std::move(n));
    return nodes.back();
}

bool Graph::removeNode(const juce::String& id)
{
    const auto before = nodes.size();
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&id](const Node& n) { return n.id == id; }), nodes.end());
    connections.erase(std::remove_if(connections.begin(), connections.end(), [&id](const Connection& c) {
        return (!c.from.graphInput && c.from.node == id) || c.to.node == id;
    }), connections.end());
    return nodes.size() != before;
}

Connection& Graph::connect(const juce::String& fromNode, const juce::String& fromPin,
                           const juce::String& toNode, const juce::String& toPin)
{
    Connection c;
    c.from = { fromNode, fromPin, -1, false };
    c.to = { toNode, toPin, -1, false };
    const auto registry = definitions();
    resolveEndpoint(c.from, false, *this, registry);
    resolveEndpoint(c.to, true, *this, registry);
    connections.push_back(c);
    assignConnectionIds(connections);
    return connections.back();
}

Connection& Graph::connectGraphInput(const juce::String& inputName, const juce::String& toNode, const juce::String& toPin)
{
    Connection c;
    c.from = { inputName, inputName, 0, true };
    c.to = { toNode, toPin, -1, false };
    resolveEndpoint(c.to, true, *this, definitions());
    connections.push_back(c);
    assignConnectionIds(connections);
    return connections.back();
}

std::vector<const Connection*> Graph::connectionsInto(const juce::String& nodeId, const juce::String& pin) const
{
    std::vector<const Connection*> found;
    for (const auto& c : connections)
        if (c.to.node == nodeId && c.to.pin == pin)
            found.push_back(&c);
    return found;
}

DefinitionRegistry Graph::definitions() const
{
    DefinitionRegistry registry;
    for (const auto& s : subgraphs)
        registry.add(callNodeDefinition(s.signature));
    for (const auto& n : nodes)
    {
        if (n.type != "call_function" && !n.type.startsWith("call_function:")) continue;
        const auto ref = n.functionRef();
        if (ref.isEmpty() || findSubgraph(ref) != nullptr || registry.find("call_function:" + ref) != nullptr) continue;
        // An external function: its signature as written on the node.
        const auto fn = n.parameters["function"];
        FunctionSignature sig;
        sig.id = ref;
        sig.name = text(fn, "name", ref);
        sig.namespaceName = text(fn, "namespace");
        sig.kind = text(fn, "kind", "pure");
        sig.accessibility = "public";
        sig.inputs = portsOf(fn.getProperty("inputs", {}), "value", "any");
        if (!fn.getProperty("inputs", {}).isArray())
            for (int i = 0; i < (int)n.inputDefaults.size(); ++i)
                sig.inputs.push_back({ "arg" + juce::String(i + 1), "any", PinFlow::Data });
        sig.outputs = portsOf(fn.getProperty("outputs", {}), "result", "any");
        if (!fn.getProperty("outputs", {}).isArray())
            sig.outputs.push_back({ "result", text(fn, "returnType", "any"), PinFlow::Data });
        registry.add(callNodeDefinition(sig));
    }
    return registry;
}

bool Graph::resolveDefinition(const Node& node, const DefinitionRegistry& registry, NodeDefinition& out) const
{
    if (node.type == "call_function" || node.type.startsWith("call_function:"))
    {
        const auto* d = registry.find("call_function:" + node.functionRef());
        if (d == nullptr) return false;
        out = *d;
        return true;
    }
    const auto* d = schemaVersion == 1 ? schemaV1Definition(node.type) : nullptr;
    if (d == nullptr) d = registry.find(node.type);
    if (d == nullptr) return false;
    out = *d;
    if (node.type == "reroute")
    {
        const auto pinType = node.parameters["pinType"].toString();
        const auto flow = flowFromName(node.parameters["pinFlow"].toString());
        for (auto* pins : { &out.inputs, &out.outputs })
            for (auto& p : *pins)
            {
                if (pinType.isNotEmpty()) p.type = pinType;
                p.flow = flow;
            }
    }
    return true;
}

LoadResult parseGraph(const juce::String& json)
{
    LoadResult result;
    auto& diags = result.diagnostics;
    auto& graph = result.graph;

    juce::var root;
    const auto parsed = juce::JSON::parse(json, root);
    if (parsed.failed())
    {
        error(diags, "invalid_json", "The document is not valid JSON: " + parsed.getErrorMessage());
        return result;
    }
    if (root.getDynamicObject() == nullptr)
    {
        error(diags, "not_an_object", "The document is not a JSON object.");
        return result;
    }

    if (root.hasProperty("schemaVersion"))
    {
        const auto v = root.getProperty("schemaVersion", {});
        if (!isNumber(v) || (int)v < 1)
        {
            error(diags, "invalid_schema_version", "schemaVersion must be a positive integer, not '" + v.toString() + "'.");
            return result;
        }
        if ((int)v > currentSchemaVersion)
        {
            error(diags, "unsupported_schema_version", "schemaVersion " + v.toString() + " is newer than this build reads (up to "
                + juce::String(currentSchemaVersion) + ").");
            return result;
        }
        graph.schemaVersion = (int)v;
    }
    else
        graph.schemaVersion = 1;

    graph.document = root;
    graph.name = text(root, "name");
    graph.diagramType = text(root, "diagramType", text(root, "kind", "node_graph"));
    if (graph.diagramType == "stateMachine") graph.diagramType = "state_machine";

    const auto params = root.getProperty("params", {});
    if (!params.isVoid() && !params.isArray())
        error(diags, "invalid_section", "params must be an array.");
    if (auto* arr = params.getArray())
        for (int i = 0; i < arr->size(); ++i)
        {
            const auto& p = arr->getReference(i);
            const auto name = text(p, "name");
            if (name.isEmpty())
            {
                error(diags, "input_without_name", "Graph input #" + juce::String(i + 1) + " has no name.");
                continue;
            }
            graph.interface.inputs.push_back({ name, text(p, "type", "i64"), positionOf(p) });
        }
    graph.interface.output = text(root, "output");

    if (auto* arr = root.getProperty("subgraphs", {}).getArray())
        for (const auto& f : *arr)
        {
            GraphReference ref;
            auto& sig = ref.signature;
            sig.id = text(f, "id", text(f, "name"));
            sig.name = text(f, "name", text(f, "title", sig.id));
            sig.namespaceName = text(f, "namespace", text(root, "namespace"));
            sig.accessibility = text(f, "accessibility", "package");
            sig.kind = text(f, "kind", "pure");
            sig.inputs = portsOf(f.getProperty("inputs", f.getProperty("params", {})), "value", "i64");
            sig.outputs = portsOf(f.getProperty("outputs", {}), "result", "i64");
            if (!f.getProperty("outputs", {}).isArray() && text(f, "output").isNotEmpty())
                sig.outputs.push_back({ text(f, "output"), text(f, "returnType", "i64"), PinFlow::Data });
            ref.source = f;
            if (sig.id.isEmpty())
                error(diags, "subgraph_without_id", "A subgraph has neither an id nor a name.");
            else
                graph.subgraphs.push_back(std::move(ref));
        }

    struct PendingRef { juce::String toNode; int toIndex; Endpoint from; };
    std::vector<PendingRef> refs;

    const auto nodesVar = root.getProperty("nodes", {});
    if (!nodesVar.isVoid() && !nodesVar.isArray())
        error(diags, "invalid_section", "nodes must be an array.");
    if (auto* arr = nodesVar.getArray())
        for (int i = 0; i < arr->size(); ++i)
        {
            const auto& n = arr->getReference(i);
            if (!n.isObject())
            {
                error(diags, "invalid_node", "Node #" + juce::String(i + 1) + " is not an object.");
                continue;
            }
            Node node;
            node.id = text(n, "id");
            node.type = text(n, "type");
            if (node.id.isEmpty())
            {
                error(diags, "node_without_id", "Node #" + juce::String(i + 1) + " (" + (node.type.isNotEmpty() ? node.type : juce::String("no type"))
                    + ") has no id; it cannot be connected and was not loaded.");
                continue;
            }
            if (node.type.isEmpty())
            {
                error(diags, "node_without_type", "Node '" + node.id + "' has no type and was not loaded.", node.id);
                continue;
            }
            node.position = positionOf(n);
            for (const auto& prop : n.getDynamicObject()->getProperties())
                if (!nodeStructuralKeys.contains(prop.name.toString()))
                    node.parameters.set(prop.name, prop.value);

            const auto inputs = n.getProperty("inputs", {});
            if (!inputs.isVoid() && !inputs.isArray())
                error(diags, "invalid_inputs", "Node '" + node.id + "': inputs must be an array.", node.id);
            if (auto* in = inputs.getArray())
                for (int k = 0; k < in->size(); ++k)
                {
                    const auto& entry = in->getReference(k);
                    node.inputDefaults.push_back({});
                    if (!entry.isObject())
                    {
                        warning(diags, "invalid_input_entry", "Node '" + node.id + "' input #" + juce::String(k + 1) + " is not an object; ignored.", node.id);
                        continue;
                    }
                    if (entry.hasProperty("ref") || entry.hasProperty("param"))
                    {
                        Endpoint from;
                        from.graphInput = !entry.hasProperty("ref");
                        from.node = text(entry, from.graphInput ? "param" : "ref");
                        from.pin = text(entry, "pin");
                        from.index = from.pin.isEmpty() ? 0 : -1;   // v1/v2 refs name a node's first output
                        refs.push_back({ node.id, k, from });
                    }
                    else
                        node.inputDefaults.back() = text(entry, "default");
                }
            graph.nodes.push_back(std::move(node));
        }

    const auto connectionsVar = root.getProperty("connections", {});
    if (!connectionsVar.isVoid() && !connectionsVar.isArray())
        error(diags, "invalid_section", "connections must be an array.");
    if (auto* arr = connectionsVar.getArray())
    {
        // Explicit connections are authoritative; inputs refs are their
        // compiler-format copy.
        for (int i = 0; i < arr->size(); ++i)
        {
            const auto& c = arr->getReference(i);
            Connection conn;
            conn.id = text(c, "id");
            if (!parseEndpoint(c.getProperty("from", {}), true, conn.from) || !parseEndpoint(c.getProperty("to", {}), false, conn.to))
            {
                error(diags, "invalid_connection", "Connection #" + juce::String(i + 1) + (conn.id.isNotEmpty() ? " ('" + conn.id + "')" : juce::String())
                    + " needs from {node|input, pin|index} and to {node, pin|index}; it was not loaded.", {}, conn.id);
                continue;
            }
            graph.connections.push_back(conn);
        }
    }
    else
        for (const auto& r : refs)
        {
            Connection conn;
            conn.from = r.from;
            conn.to = { r.toNode, {}, r.toIndex, false };
            graph.connections.push_back(conn);
        }

    const auto registry = graph.definitions();
    for (auto& c : graph.connections)
    {
        resolveEndpoint(c.from, false, graph, registry);
        resolveEndpoint(c.to, true, graph, registry);
    }
    assignConnectionIds(graph.connections);

    result.ok = true;
    return result;
}

LoadResult loadGraph(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        LoadResult r;
        error(r.diagnostics, "file_not_found", "No such file: " + file.getFullPathName());
        return r;
    }
    return parseGraph(file.loadFileAsString());
}

juce::var toVar(const Graph& graph)
{
    juce::var root = graph.document.isObject() ? graph.document.clone() : juce::var(new juce::DynamicObject());
    auto* obj = root.getDynamicObject();
    const bool v1Shapes = usesSchemaV1Shapes(graph);
    obj->setProperty("schemaVersion", v1Shapes ? 1 : currentSchemaVersion);
    if (graph.name.isNotEmpty() || obj->hasProperty("name")) obj->setProperty("name", graph.name);
    if (!v1Shapes || obj->hasProperty("diagramType")) obj->setProperty("diagramType", graph.diagramType);
    obj->removeProperty("kind");   // old spelling of diagramType

    if (!graph.interface.inputs.empty() || obj->hasProperty("params"))
    {
        juce::Array<juce::var> params;
        for (const auto& p : graph.interface.inputs)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("name", p.name);
            o->setProperty("type", p.type);
            writePosition(*o, p.position);
            params.add(juce::var(o));
        }
        obj->setProperty("params", params);
    }
    if (graph.interface.output.isNotEmpty() || obj->hasProperty("output"))
        obj->setProperty("output", graph.interface.output);

    const auto registry = graph.definitions();
    juce::Array<juce::var> nodes;
    for (const auto& n : graph.nodes)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", n.id);
        o->setProperty("type", n.type);
        writePosition(*o, n.position);
        for (const auto& p : n.parameters)
            o->setProperty(p.name, p.value.clone());

        int inputCount = (int)n.inputDefaults.size();
        NodeDefinition d;
        if (graph.resolveDefinition(n, registry, d))
            inputCount = std::max(inputCount, (int)d.inputs.size());
        for (const auto& c : graph.connections)
            if (c.to.node == n.id)
                inputCount = std::max(inputCount, c.to.index + 1);

        if (inputCount > 0)
        {
            juce::Array<juce::var> inputs;
            for (int i = 0; i < inputCount; ++i)
            {
                auto* entry = new juce::DynamicObject();
                auto it = std::find_if(graph.connections.begin(), graph.connections.end(), [&n, i](const Connection& c) {
                    return c.to.node == n.id && c.to.index == i;
                });
                if (it == graph.connections.end())
                    entry->setProperty("default", i < (int)n.inputDefaults.size() ? n.inputDefaults[(size_t)i] : juce::String());
                else if (it->from.graphInput)
                    entry->setProperty("param", it->from.node);
                else
                {
                    entry->setProperty("ref", it->from.node);
                    if (it->from.pin.isNotEmpty()) entry->setProperty("pin", it->from.pin);
                }
                inputs.add(juce::var(entry));
            }
            o->setProperty("inputs", inputs);
        }
        nodes.add(juce::var(o));
    }
    obj->setProperty("nodes", nodes);

    juce::Array<juce::var> connections;
    for (const auto& c : graph.connections)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", c.id);
        auto* from = new juce::DynamicObject();
        from->setProperty(c.from.graphInput ? "input" : "node", c.from.node);
        if (c.from.pin.isNotEmpty()) from->setProperty("pin", c.from.pin);
        if (c.from.index >= 0) from->setProperty("index", c.from.index);
        auto* to = new juce::DynamicObject();
        to->setProperty("node", c.to.node);
        if (c.to.pin.isNotEmpty()) to->setProperty("pin", c.to.pin);
        if (c.to.index >= 0) to->setProperty("index", c.to.index);
        o->setProperty("from", juce::var(from));
        o->setProperty("to", juce::var(to));
        connections.add(juce::var(o));
    }
    obj->setProperty("connections", connections);

    if (!graph.subgraphs.empty() || obj->hasProperty("subgraphs"))
    {
        juce::Array<juce::var> subgraphs;
        for (const auto& s : graph.subgraphs)
        {
            juce::var v = s.source.isObject() ? s.source.clone() : juce::var(new juce::DynamicObject());
            auto* o = v.getDynamicObject();
            o->setProperty("id", s.signature.id);
            o->setProperty("name", s.signature.name);
            o->setProperty("namespace", s.signature.namespaceName);
            o->setProperty("kind", s.signature.kind);
            o->setProperty("accessibility", s.signature.accessibility);
            o->setProperty("inputs", portsVar(s.signature.inputs));
            o->setProperty("outputs", portsVar(s.signature.outputs));
            subgraphs.add(v);
        }
        obj->setProperty("subgraphs", subgraphs);
    }
    return root;
}

juce::String toJson(const Graph& graph)
{
    return juce::JSON::toString(toVar(graph));
}

bool saveGraph(const Graph& graph, const juce::File& file)
{
    return file.replaceWithText(toJson(graph));
}

std::vector<Diagnostic> validate(const Graph& graph)
{
    return validate(graph, graph.definitions());
}

std::vector<Diagnostic> validate(const Graph& graph, const DefinitionRegistry& registry)
{
    std::vector<Diagnostic> diags;

    std::set<juce::String> inputNames;
    for (const auto& p : graph.interface.inputs)
        if (!inputNames.insert(p.name).second)
            error(diags, "duplicate_input", "Graph input '" + p.name + "' is declared more than once.");

    std::map<juce::String, NodeDefinition> defs;
    std::set<juce::String> nodeIds;
    for (const auto& n : graph.nodes)
    {
        if (!nodeIds.insert(n.id).second)
            error(diags, "duplicate_node_id", "Node id '" + n.id + "' is used by more than one node.", n.id);
        if (inputNames.count(n.id) != 0)
            error(diags, "id_collision", "Node id '" + n.id + "' is also the name of a graph input, so references to it are ambiguous.", n.id);

        const bool isCall = n.type == "call_function" || n.type.startsWith("call_function:");
        if (isCall)
        {
            const auto ref = n.functionRef();
            if (ref.isEmpty())
            {
                error(diags, "missing_parameter", "Node '" + n.id + "' calls a function but does not say which (function.id).", n.id);
                continue;
            }
            if (graph.findSubgraph(ref) == nullptr && text(n.parameters["function"], "pod").isEmpty())
                error(diags, "undefined_function", "Node '" + n.id + "' calls '" + ref
                    + "', which is not a subgraph of this graph and names no owning pod.", n.id);
        }

        NodeDefinition d;
        if (!graph.resolveDefinition(n, registry, d))
        {
            if (!isCall)
                error(diags, "unknown_node_type", "Node '" + n.id + "' has type '" + n.type + "', which is not a known node definition.", n.id);
            continue;
        }
        if (!isCall)
            for (const auto& p : d.parameters)
                if (p.required && n.parameters[juce::Identifier(p.name)].toString().trim().isEmpty())
                    error(diags, "missing_parameter", "Node '" + n.id + "' (" + d.title + ") needs a value for '" + p.name + "'.", n.id);
        defs[n.id] = d;
    }

    if (graph.interface.output.isNotEmpty() && graph.findNode(graph.interface.output) == nullptr)
        error(diags, "invalid_output", "The graph output names node '" + graph.interface.output + "', which does not exist.");

    std::set<juce::String> connectionIds;
    std::map<std::pair<juce::String, int>, std::vector<const Connection*>> drivers;
    for (const auto& c : graph.connections)
    {
        if (!connectionIds.insert(c.id).second)
            error(diags, "duplicate_connection_id", "Connection id '" + c.id + "' is used more than once.", {}, c.id);

        PinDefinition fromPin;
        bool fromKnown = false;
        if (c.from.graphInput)
        {
            if (const auto* p = graph.findInput(c.from.node))
            {
                fromPin = { p->name, p->type, PinFlow::Data };
                fromKnown = true;
            }
            else
                error(diags, "missing_node", "Connection '" + c.id + "' leaves graph input '" + c.from.node + "', which is not declared.", {}, c.id);
        }
        else if (graph.findNode(c.from.node) == nullptr)
            error(diags, "missing_node", "Connection '" + c.id + "' leaves node '" + c.from.node + "', which does not exist.", {}, c.id);
        else if (auto it = defs.find(c.from.node); it != defs.end())
        {
            const auto& d = it->second;
            if (const auto* p = c.from.pin.isNotEmpty() ? d.findOutput(c.from.pin)
                                                        : (c.from.index >= 0 && c.from.index < (int)d.outputs.size() ? &d.outputs[(size_t)c.from.index] : nullptr))
            {
                fromPin = *p;
                fromKnown = true;
            }
            else if (c.from.pin.isNotEmpty() && d.findInput(c.from.pin) != nullptr)
                error(diags, "invalid_direction", "Connection '" + c.id + "' leaves '" + c.from.node + "." + c.from.pin
                    + "', which is an input; connections run from an output to an input.", c.from.node, c.id);
            else
                error(diags, "missing_port", "Connection '" + c.id + "': node '" + c.from.node + "' (" + d.title + ") has no output '"
                    + endpointText(c.from).fromFirstOccurrenceOf(".", false, false) + "'.", c.from.node, c.id);
        }

        PinDefinition toPin;
        bool toKnown = false;
        int toIndex = c.to.index;
        if (graph.findNode(c.to.node) == nullptr)
            error(diags, "missing_node", "Connection '" + c.id + "' enters node '" + c.to.node + "', which does not exist.", {}, c.id);
        else if (auto it = defs.find(c.to.node); it != defs.end())
        {
            const auto& d = it->second;
            if (toIndex < 0 && c.to.pin.isNotEmpty()) toIndex = d.inputIndex(c.to.pin);
            const bool inRange = toIndex >= 0 && (toIndex < (int)d.inputs.size() || d.variadicInputs);
            const bool nameMatches = c.to.pin.isEmpty() || c.to.pin == d.inputName(toIndex);
            if (inRange && nameMatches)
            {
                toPin = toIndex < (int)d.inputs.size() ? d.inputs[(size_t)toIndex] : PinDefinition { d.inputName(toIndex), "any", PinFlow::Data };
                toKnown = true;
            }
            else if (c.to.pin.isNotEmpty() && d.findOutput(c.to.pin) != nullptr && d.findInput(c.to.pin) == nullptr)
                error(diags, "invalid_direction", "Connection '" + c.id + "' enters '" + c.to.node + "." + c.to.pin
                    + "', which is an output; connections run from an output to an input.", c.to.node, c.id);
            else
                error(diags, "missing_port", "Connection '" + c.id + "': node '" + c.to.node + "' (" + d.title + ") has no input '"
                    + endpointText(c.to).fromFirstOccurrenceOf(".", false, false) + "'.", c.to.node, c.id);
        }

        if (fromKnown && toKnown)
        {
            if (!pinsCompatible(fromPin, toPin))
                error(diags, "incompatible_types", "Connection '" + c.id + "' joins " + flowName(fromPin.flow) + " " + fromPin.type
                    + " to " + flowName(toPin.flow) + " " + toPin.type + ".", c.to.node, c.id);
            auto& list = drivers[{ c.to.node, toIndex }];
            list.push_back(&c);
            const auto& d = defs[c.to.node];
            if (list.size() == 2 && !(d.multiDriveExecInputs && toPin.flow == PinFlow::Exec))
                error(diags, "input_multiply_driven", "Input '" + c.to.node + "." + toPin.name + "' is driven by more than one connection ('"
                    + list[0]->id + "', '" + list[1]->id + "').", c.to.node, c.id);
        }
    }
    return diags;
}

void carryForwardUnmanaged(juce::var& written, const juce::var& previous,
                           const juce::StringArray& managedRootKeys,
                           const juce::StringArray& managedNodeKeys)
{
    auto* out = written.getDynamicObject();
    auto* old = previous.getDynamicObject();
    if (out == nullptr || old == nullptr) return;

    for (const auto& prop : old->getProperties())
        if (!managedRootKeys.contains(prop.name.toString()) && !out->hasProperty(prop.name))
            out->setProperty(prop.name, prop.value.clone());

    std::map<juce::String, juce::DynamicObject*> oldNodes;
    if (auto* arr = old->getProperty("nodes").getArray())
        for (const auto& n : *arr)
            if (auto* o = n.getDynamicObject())
                oldNodes[o->getProperty("id").toString()] = o;
    if (auto* arr = out->getProperty("nodes").getArray())
        for (auto& n : *arr)
            if (auto* o = n.getDynamicObject())
                if (auto it = oldNodes.find(o->getProperty("id").toString()); it != oldNodes.end())
                    for (const auto& prop : it->second->getProperties())
                        if (!managedNodeKeys.contains(prop.name.toString()) && !o->hasProperty(prop.name))
                            o->setProperty(prop.name, prop.value.clone());
}
}
