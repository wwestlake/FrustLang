// frust_node_graph regression tests. No window and no compiler: everything
// here is what another application can do with a node graph on its own.

#include <frust_node_graph/NodeGraph.h>

#include <algorithm>
#include <cstdio>
#include <set>
#include <tuple>

using namespace frust_node_graph;

namespace
{
int failures = 0;
int checks = 0;

void check(bool ok, const char* name, const juce::String& detail = {})
{
    ++checks;
    if (!ok) ++failures;
    std::printf("%s  %s%s\n", ok ? "PASS" : "FAIL", name, ok || detail.isEmpty() ? "" : ("  -- " + detail).toRawUTF8());
}

juce::File fixture(const char* name) { return juce::File(FRUST_NODE_GRAPH_FIXTURES).getChildFile(name); }

int errorCount(const std::vector<Diagnostic>& d)
{
    int n = 0;
    for (const auto& x : d) n += x.isError() ? 1 : 0;
    return n;
}

bool hasCode(const std::vector<Diagnostic>& d, const char* code)
{
    for (const auto& x : d)
        if (x.code == code) return true;
    return false;
}

juce::String summary(const std::vector<Diagnostic>& d)
{
    juce::StringArray lines;
    for (const auto& x : d) lines.add(describe(x));
    return lines.joinIntoString(" | ");
}

const Connection* findConnection(const Graph& g, const juce::String& fromNode, const juce::String& fromPin,
                                 const juce::String& toNode, const juce::String& toPin)
{
    for (const auto& c : g.connections)
        if (c.from.node == fromNode && c.from.pin == fromPin && c.to.node == toNode && c.to.pin == toPin)
            return &c;
    return nullptr;
}

Graph roundTrip(const Graph& g)
{
    auto r = parseGraph(toJson(g));
    return r.graph;
}

// The compiler's view of a document: every node's type, value and inputs
// refs/params, which is what node_compiler reads.
juce::String compilerView(const juce::var& root)
{
    juce::String s;
    s << root.getProperty("functionName", {}).toString() << "|" << root.getProperty("output", {}).toString() << "|";
    if (auto* params = root.getProperty("params", {}).getArray())
        for (const auto& p : *params) s << p.getProperty("name", {}).toString() << ":" << p.getProperty("type", {}).toString() << ",";
    if (auto* nodes = root.getProperty("nodes", {}).getArray())
        for (const auto& n : *nodes)
        {
            s << "\n" << n.getProperty("id", {}).toString() << "=" << n.getProperty("type", {}).toString()
              << " v=" << n.getProperty("value", {}).toString() << " t=" << n.getProperty("text", {}).toString()
              << " f=" << n.getProperty("function", {}).toString() << " (";
            if (auto* in = n.getProperty("inputs", {}).getArray())
                for (const auto& e : *in)
                    s << (e.hasProperty("ref") ? "ref:" + e.getProperty("ref", {}).toString()
                          : e.hasProperty("param") ? "param:" + e.getProperty("param", {}).toString() : juce::String("default")) << " ";
            s << ")";
        }
    return s;
}

// A graph with data and exec flow, a multi-output node and a graph input.
Graph buildSample()
{
    Graph g;
    g.name = "Sample";
    g.interface.inputs.push_back({ "x", "i64", { 10.0f, 20.0f, true } });

    auto& ten = g.addNode("literal_i64", "ten");
    ten.parameters.set("value", 10);
    ten.position = { 40.0f, 200.0f, true };
    auto& sum = g.addNode("add", "sum");
    sum.position = { 220.0f, 120.0f, true };
    auto& cond = g.addNode("gt", "big");
    cond.position = { 400.0f, 120.0f, true };
    auto& start = g.addNode("event_start", "start");
    start.position = { 0.0f, 0.0f, true };
    auto& branch = g.addNode("branch", "choose");
    branch.position = { 600.0f, 60.0f, true };
    auto& text = g.addNode("literal_string", "msg");
    text.parameters.set("text", "small");
    auto& print = g.addNode("print", "say");
    auto& end = g.addNode("end", "done");
    auto& loop = g.addNode("for_loop", "loop");
    auto& twice = g.addNode("mul", "twice");
    g.interface.output = "sum";

    g.connectGraphInput("x", "sum", "a");
    g.connect("ten", "value", "sum", "b");
    g.connect("sum", "sum", "big", "a");
    g.connect("ten", "value", "big", "b");
    g.connect("start", "start", "choose", "in");
    g.connect("big", "is greater", "choose", "condition");
    g.connect("choose", "false", "say", "in");          // second exec output
    g.connect("msg", "value", "say", "value");
    g.connect("say", "then", "done", "in");
    g.connect("choose", "true", "loop", "in");
    g.connect("loop", "index", "twice", "a");            // third output, a data pin after two exec pins
    g.connect("ten", "value", "twice", "b");
    juce::ignoreUnused(twice, end);
    return g;
}
}

int main()
{
    std::printf("-- node definitions --\n");
    {
        const auto& defs = builtinDefinitions();
        std::set<juce::String> types;
        bool unique = true;
        for (const auto& d : defs) unique = types.insert(d.type).second && unique;
        check(unique, "every built-in node type is defined once");
        check(types.count("print") && types.count("branch") && types.count("sm_state") && types.count("reroute") && types.count("call"),
              "editor and node_compiler types are both defined");
        const PinDefinition execOut { "then", "exec", PinFlow::Exec }, dataIn { "value", "i64", PinFlow::Data }, anyIn { "v", "any", PinFlow::Data };
        check(!pinsCompatible(execOut, dataIn), "exec does not wire to data");
        check(pinsCompatible({ "o", "i64", PinFlow::Data }, anyIn) && !pinsCompatible({ "o", "bool", PinFlow::Data }, dataIn),
              "data wiring: same type or any");
        check(schemaV1Definition("print") != nullptr && schemaV1Definition("print")->inputs.size() == 1, "schema v1 print has the compiler's one-input shape");
    }

    std::printf("-- build, edit, save, reopen --\n");
    {
        auto g = buildSample();
        auto diags = validate(g);
        check(errorCount(diags) == 0, "the sample graph validates", summary(diags));
        check(g.connections.size() == 12, "12 connections made", juce::String((int)g.connections.size()));

        const auto json = toJson(g);
        auto loaded = parseGraph(json);
        check(loaded.ok && loaded.diagnostics.empty(), "the saved document reloads cleanly", summary(loaded.diagnostics));
        const auto& r = loaded.graph;
        check(r.schemaVersion == currentSchemaVersion, "written as the current schema version");
        check(r.nodes.size() == g.nodes.size(), "all nodes survive");
        bool same = r.nodes.size() == g.nodes.size();
        for (size_t i = 0; same && i < g.nodes.size(); ++i)
        {
            const auto& a = g.nodes[i];
            const auto& b = r.nodes[i];
            same = a.id == b.id && a.type == b.type && a.position.present == b.position.present
                && a.position.x == b.position.x && a.position.y == b.position.y;
        }
        check(same, "node ids, types and positions survive in order");
        check(r.findNode("ten")->parameters["value"].toString() == "10" && r.findNode("msg")->parameters["text"].toString() == "small",
              "parameters survive");
        bool connectionsSame = r.connections.size() == g.connections.size();
        for (size_t i = 0; connectionsSame && i < g.connections.size(); ++i)
            connectionsSame = r.connections[i].id == g.connections[i].id && r.connections[i].from.pin == g.connections[i].from.pin
                && r.connections[i].to.pin == g.connections[i].to.pin && r.connections[i].from.graphInput == g.connections[i].from.graphInput;
        check(connectionsSame, "connection ids and pins survive");
        check(findConnection(r, "choose", "false", "say", "in") != nullptr, "a wire from the second exec output stays on that output");
        check(findConnection(r, "loop", "index", "twice", "a") != nullptr && findConnection(r, "loop", "index", "twice", "a")->from.index == 2,
              "a wire from the third output (for-loop index) stays on it");
        check(r.interface.inputs.size() == 1 && r.interface.inputs[0].position.present && r.interface.output == "sum", "graph interface survives");

        // Edit: move a node, change a parameter, rewire an input.
        auto e = r;
        e.findNode("sum")->position = { 333.0f, 444.0f, true };
        e.findNode("ten")->parameters.set("value", 25);
        e.connections.erase(std::remove_if(e.connections.begin(), e.connections.end(), [](const Connection& c) {
            return c.to.node == "twice" && c.to.pin == "b";
        }), e.connections.end());
        e.connect("sum", "sum", "twice", "b");
        const auto e2 = roundTrip(e);
        check(e2.findNode("sum")->position.x == 333.0f && e2.findNode("sum")->position.y == 444.0f, "a moved node keeps its new position");
        check((int)e2.findNode("ten")->parameters["value"] == 25, "an edited parameter keeps its new value");
        check(findConnection(e2, "sum", "sum", "twice", "b") != nullptr && findConnection(e2, "ten", "value", "twice", "b") == nullptr,
              "a rewired input keeps its new source");
        check(errorCount(validate(e2)) == 0, "the edited graph still validates", summary(validate(e2)));

        // Saving twice gives the same document: ids are stable.
        check(toJson(e2) == toJson(roundTrip(e2)), "save -> load -> save is stable");

        const auto root = juce::JSON::parse(json);
        juce::String sayInputs;
        if (auto* nodes = root.getProperty("nodes", {}).getArray())
            for (const auto& n : *nodes)
                if (n.getProperty("id", {}).toString() == "sum")
                    sayInputs = juce::JSON::toString(n.getProperty("inputs", {}), true);
        check(sayInputs.contains("\"param\": \"x\"") && sayInputs.contains("\"ref\": \"ten\""),
              "nodes still carry compiler-format inputs refs", sayInputs);
    }

    std::printf("-- state machine: several transitions into one state --\n");
    {
        Graph g;
        g.diagramType = "state_machine";
        for (auto id : { "idle", "busy", "error" })
        {
            auto& s = g.addNode("sm_state", id);
            s.parameters.set("text", id);
            s.parameters.set("initial", juce::String(id) == "idle");
        }
        for (auto [t, from, to] : { std::tuple { "go", "idle", "busy" }, std::tuple { "fail", "busy", "error" }, std::tuple { "reset", "error", "idle" }, std::tuple { "stop", "busy", "idle" } })
        {
            auto& n = g.addNode("sm_transition", t);
            n.parameters.set("event", t);
            g.connect(from, "leave", t, "from");
            g.connect(t, "to", to, "enter");
        }
        const auto r = roundTrip(g);
        check(r.connectionsInto("idle", "enter").size() == 2, "both transitions into 'idle' survive a save");
        check(errorCount(validate(r)) == 0, "a state entered by two transitions is valid", summary(validate(r)));
    }

    std::printf("-- structural validation --\n");
    {
        auto g = buildSample();
        auto bad = g;
        bad.connect("start", "start", "twice", "a");   // exec into data, and twice.a now driven twice
        auto d = validate(bad);
        check(hasCode(d, "incompatible_types"), "exec output into a data input is rejected", summary(d));
        check(hasCode(d, "input_multiply_driven"), "an input driven twice is rejected");

        bad = g;
        bad.connections.push_back({ "c_dir", { "sum", "sum", 0, false }, { "big", "is greater", -1, false } });
        check(hasCode(validate(bad), "invalid_direction"), "a wire into an output is rejected", summary(validate(bad)));
        bad = g;
        bad.connections.push_back({ "c_dir2", { "sum", "a", -1, false }, { "twice", "b", -1, false } });
        check(hasCode(validate(bad), "invalid_direction"), "a wire out of an input is rejected");
        bad = g;
        bad.connections.push_back({ "c_port", { "sum", "total", -1, false }, { "big", "a", -1, false } });
        check(hasCode(validate(bad), "missing_port"), "a missing pin is reported");
        bad = g;
        bad.connections.push_back({ "c_node", { "ghost", "value", -1, false }, { "big", "a", -1, false } });
        check(hasCode(validate(bad), "missing_node"), "a missing node is reported");
        bad = g;
        bad.connections.push_back({ "c_in", { "y", "y", 0, true }, { "big", "a", -1, false } });
        check(hasCode(validate(bad), "missing_node"), "an undeclared graph input is reported");
        bad = g;
        bad.connections.push_back(bad.connections.front());
        check(hasCode(validate(bad), "duplicate_connection_id"), "a duplicate connection id is reported");
        bad = g;
        bad.nodes.push_back(bad.nodes.front());
        check(hasCode(validate(bad), "duplicate_node_id"), "a duplicate node id is reported");
        bad = g;
        bad.addNode("x", "x").type = "add";   // uniqueNodeId avoids it, so force the collision
        bad.nodes.back().id = "x";
        check(hasCode(validate(bad), "id_collision"), "a node named like a graph input is reported");
        bad = g;
        bad.addNode("warp_drive", "w");
        check(hasCode(validate(bad), "unknown_node_type"), "an unknown node type is reported");
        bad = g;
        bad.addNode("sm_transition", "t");
        check(hasCode(validate(bad), "missing_parameter"), "a transition without an event is reported");
        bad = g;
        bad.addNode("call_function", "c").parameters.set("function", juce::JSON::parse(R"({"id":"nowhere"})"));
        check(hasCode(validate(bad), "undefined_function"), "a call to an undefined function is reported", summary(validate(bad)));
        bad = g;
        bad.interface.output = "missing";
        check(hasCode(validate(bad), "invalid_output"), "an output naming a missing node is reported");

        auto withFn = g;
        auto ref = GraphReference {};
        ref.signature.id = "scale";
        ref.signature.name = "scale";
        ref.signature.inputs = { { "v", "i64", PinFlow::Data } };
        ref.signature.outputs = { { "scaled", "i64", PinFlow::Data } };
        withFn.subgraphs.push_back(ref);
        withFn.addNode("call_function", "scale_it").parameters.set("function", juce::JSON::parse(R"({"id":"scale"})"));
        withFn.connect("sum", "sum", "scale_it", "v");
        const auto fnRound = roundTrip(withFn);
        check(errorCount(validate(fnRound)) == 0 && findConnection(fnRound, "sum", "sum", "scale_it", "v") != nullptr,
              "a call to one of the graph's own functions resolves its pins", summary(validate(fnRound)));
    }

    std::printf("-- malformed and unsupported documents --\n");
    {
        auto r = parseGraph("{ not json");
        check(!r.ok && hasCode(r.diagnostics, "invalid_json"), "invalid JSON is refused with a reason", summary(r.diagnostics));
        r = parseGraph("[1, 2]");
        check(!r.ok && hasCode(r.diagnostics, "not_an_object"), "a non-object document is refused", summary(r.diagnostics));
        r = parseGraph(R"({"schemaVersion": 9, "nodes": []})");
        check(!r.ok && hasCode(r.diagnostics, "unsupported_schema_version"), "a newer schema version is refused, not misread", summary(r.diagnostics));
        r = parseGraph(R"({"schemaVersion": "two"})");
        check(!r.ok && hasCode(r.diagnostics, "invalid_schema_version"), "a non-numeric schema version is refused");
        r = parseGraph(R"({"schemaVersion": 2, "nodes": [{"type": "add"}, {"id": "n"}, 5, {"id": "ok", "type": "add", "inputs": 3}]})");
        check(r.ok && hasCode(r.diagnostics, "node_without_id") && hasCode(r.diagnostics, "node_without_type")
                  && hasCode(r.diagnostics, "invalid_node") && hasCode(r.diagnostics, "invalid_inputs") && r.graph.nodes.size() == 1,
              "bad nodes are reported one by one; good ones still load", summary(r.diagnostics));
        r = parseGraph(R"({"schemaVersion": 3, "nodes": [], "connections": [{"id": "c", "from": {"node": "a"}}]})");
        check(r.ok && hasCode(r.diagnostics, "invalid_connection"), "an incomplete connection is reported");
    }

    std::printf("-- nothing executable is dropped --\n");
    {
        const auto doc = R"({"schemaVersion": 2, "namespace": "djehuti.test", "groups": [{"id": "g1", "title": "Math"}],
            "constants": [{"id": "k", "value": 3}], "futureSection": {"keep": true},
            "nodes": [{"id": "w", "type": "warp_drive", "x": 5, "y": 6, "power": 11, "context": "audio", "inputs": [{"default": "7"}]},
                      {"id": "n", "type": "literal_i64", "value": 4, "context": "worker"}]})";
        const auto r = parseGraph(doc);
        const auto saved = juce::JSON::parse(toJson(r.graph));
        check(saved.getProperty("namespace", {}).toString() == "djehuti.test" && saved.getProperty("groups", {}).isArray()
                  && saved.getProperty("constants", {}).isArray() && (bool)saved.getProperty("futureSection", {}).getProperty("keep", false),
              "sections the model does not interpret are saved back");
        const auto w = saved.getProperty("nodes", {})[0];
        check(w.getProperty("type", {}).toString() == "warp_drive" && (int)w.getProperty("power", 0) == 11 && w.getProperty("context", {}).toString() == "audio"
                  && w.getProperty("inputs", {})[0].getProperty("default", {}).toString() == "7",
              "an unknown node keeps its type, parameters and input values");
        check(saved.getProperty("nodes", {})[1].getProperty("context", {}).toString() == "worker", "unmodelled node keys are kept");
    }

    std::printf("-- existing documents --\n");
    {
        for (auto name : { "simple_arithmetic.json", "branch.json", "call_and_print.json" })
        {
            const auto file = fixture(name);
            const auto r = loadGraph(file);
            const auto d = validate(r.graph);
            check(r.ok && r.diagnostics.empty() && errorCount(d) == 0, (juce::String("node_compiler example loads and validates: ") + name).toRawUTF8(),
                  summary(r.diagnostics) + " " + summary(d));
            const auto before = compilerView(juce::JSON::parse(file.loadFileAsString()));
            const auto after = compilerView(toVar(r.graph));
            check(before == after, (juce::String("its compiler view is unchanged by a save: ") + name).toRawUTF8(), before + "\n---\n" + after);
        }
        {
            const auto r = loadGraph(fixture("call_and_print.json"));
            check(r.graph.schemaVersion == 1 && (int)toVar(r.graph).getProperty("schemaVersion", 0) == 1,
                  "a v1 document using the compiler's print shape stays schemaVersion 1");
            check((int)toVar(loadGraph(fixture("branch.json")).graph).getProperty("schemaVersion", 0) == currentSchemaVersion,
                  "other v1 documents are written as the current version");
        }
        {
            const auto file = fixture("ide_hello_nodes_v2.json");
            const auto r = loadGraph(file);
            const auto d = validate(r.graph);
            check(r.ok && r.graph.schemaVersion == 2 && r.graph.nodes.size() == 4 && errorCount(d) == 0,
                  "a v2 file saved by the IDE loads and validates", summary(r.diagnostics) + " " + summary(d));
            check(findConnection(r.graph, "event_start", "start", "print", "in") && findConnection(r.graph, "text", "value", "print", "value")
                      && findConnection(r.graph, "print", "then", "end", "in"),
                  "its wires resolve to named pins");
            const auto saved = toVar(r.graph);
            const auto original = juce::JSON::parse(file.loadFileAsString());
            check(juce::JSON::toString(saved.getProperty("variables", {})) == juce::JSON::toString(original.getProperty("variables", {}))
                      && juce::JSON::toString(saved.getProperty("types", {})) == juce::JSON::toString(original.getProperty("types", {})),
                  "its variables and types are saved back unchanged");
            check(compilerView(saved) == compilerView(original), "its compiler view is unchanged by a save");
        }
        {
            const auto file = fixture("state_machine.json");
            const auto r = loadGraph(file);
            const auto saved = toVar(r.graph);
            const auto original = juce::JSON::parse(file.loadFileAsString());
            check(r.ok && r.graph.diagramType == "state_machine" && errorCount(validate(r.graph)) == 0, "a state-machine schematic loads");
            check(juce::JSON::toString(saved.getProperty("states", {})) == juce::JSON::toString(original.getProperty("states", {}))
                      && juce::JSON::toString(saved.getProperty("transitions", {})) == juce::JSON::toString(original.getProperty("transitions", {})),
                  "its states and transitions are saved back unchanged");
        }
    }

    std::printf("-- editors that write the document themselves --\n");
    {
        const auto previous = juce::JSON::parse(R"({"name": "a", "groups": [1], "nodes": [{"id": "s", "type": "sm_state", "entryAction": "beep", "context": "main"}]})");
        auto written = juce::JSON::parse(R"({"name": "a", "nodes": [{"id": "s", "type": "sm_state"}]})");
        carryForwardUnmanaged(written, previous, { "name", "nodes" }, { "id", "type", "entryAction" });
        const auto node = written.getProperty("nodes", {})[0];
        check(written.getProperty("groups", {}).isArray() && node.getProperty("context", {}).toString() == "main",
              "unmanaged sections and node keys are carried forward");
        check(!node.hasProperty("entryAction"), "a managed key the editor cleared stays cleared");
    }

    std::printf("%d checks, %s\n", checks, failures == 0 ? "ALL PASSED" : (juce::String(failures) + " FAILED").toRawUTF8());
    return failures == 0 ? 0 : 1;
}
