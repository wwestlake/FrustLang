#pragma once

// The FRust node graph document, independent of any editor: load, inspect,
// modify, validate and save a node schematic without a window.
//
// File format: the node schematic JSON the FrustIDE node designer has always
// written (NODE_SCHEMATIC_SCHEMA_V2.md in 10_node_compiler), read back to
// schemaVersion 1 (the node_compiler format). schemaVersion 3 adds one
// section, `connections`, so a wire records which output pin it leaves from
// and an input may take several wires; every node's `inputs` array is still
// written, so node_compiler reads a version 3 file exactly as before.
//
// What executes is the nodes, their types and parameters, the connections
// and the graph interface. Positions are editor layout only. Sections this
// model does not interpret (variables, types, states, targetOptions, debug,
// anything newer) are kept as written and saved back unchanged.

#include <frust_node_graph/NodeDefinitions.h>

#include <vector>

namespace frust_node_graph
{
constexpr int currentSchemaVersion = 3;

// Editor layout. Never consulted for execution.
struct Position
{
    float x = 0.0f;
    float y = 0.0f;
    bool present = false;
};

struct Node
{
    juce::String id;
    juce::String type;
    // Everything on the node besides id, type, position and inputs: its
    // parameters (value, text, event, function, ...) exactly as stored.
    juce::NamedValueSet parameters;
    // Value of each unwired input, by input index ("" when none is stored).
    std::vector<juce::String> inputDefaults;
    Position position;

    // For call_function nodes, the id of the function called.
    juce::String functionRef() const;
};

// One end of a connection. A connection may leave a graph input (one of
// the graph's params) instead of a node.
struct Endpoint
{
    juce::String node;       // node id, or graph input name when graphInput
    juce::String pin;        // pin name; empty when the file did not say
    int index = -1;          // pin index; -1 when not known
    bool graphInput = false;
};

struct Connection
{
    juce::String id;
    Endpoint from;
    Endpoint to;
};

struct InterfacePort
{
    juce::String name;
    juce::String type;
    Position position;
};

struct GraphInterface
{
    std::vector<InterfacePort> inputs;   // the generated function's params
    juce::String output;                 // id of the node whose value is returned
};

// A function the graph defines (a subgraph): its signature, and its own
// body as stored when it has one.
struct GraphReference
{
    FunctionSignature signature;
    juce::var source;   // the subgraph object as stored
};

struct Diagnostic
{
    enum class Severity { Error, Warning };
    Severity severity = Severity::Error;
    juce::String code;
    juce::String message;
    juce::String nodeId;
    juce::String connectionId;

    bool isError() const { return severity == Severity::Error; }
};

juce::String describe(const Diagnostic& d);

class Graph
{
public:
    int schemaVersion = currentSchemaVersion;  // as read; saving writes currentSchemaVersion
    juce::String name;
    juce::String diagramType = "node_graph";
    GraphInterface interface;
    std::vector<Node> nodes;
    std::vector<Connection> connections;
    std::vector<GraphReference> subgraphs;
    // The whole document as read. Sections not modelled above are saved
    // back from here.
    juce::var document;

    Node* findNode(const juce::String& id);
    const Node* findNode(const juce::String& id) const;
    const InterfacePort* findInput(const juce::String& name) const;
    const GraphReference* findSubgraph(const juce::String& id) const;

    // An id not used by any node or graph input, derived from `base`.
    juce::String uniqueNodeId(const juce::String& base) const;
    Node& addNode(const juce::String& type, const juce::String& idBase);
    // Removes the node and every connection touching it.
    bool removeNode(const juce::String& id);
    // Adds a connection (pins by name) with a stable id; returns it.
    Connection& connect(const juce::String& fromNode, const juce::String& fromPin,
                        const juce::String& toNode, const juce::String& toPin);
    Connection& connectGraphInput(const juce::String& inputName,
                                  const juce::String& toNode, const juce::String& toPin);
    std::vector<const Connection*> connectionsInto(const juce::String& nodeId, const juce::String& pin) const;

    // The node definitions this graph resolves against: the built-ins plus a
    // call_function definition for each subgraph and for each external
    // function a call_function node names.
    DefinitionRegistry definitions() const;
    // The definition of one node: its type's definition, with a reroute's
    // stored pin type and a variadic call's argument count applied.
    // Returns false when the type is unknown.
    bool resolveDefinition(const Node& node, const DefinitionRegistry& registry, NodeDefinition& out) const;
};

struct LoadResult
{
    bool ok = false;   // false: the document could not be read at all
    Graph graph;
    std::vector<Diagnostic> diagnostics;   // problems found while reading
};

LoadResult parseGraph(const juce::String& json);
LoadResult loadGraph(const juce::File& file);

juce::var toVar(const Graph& graph);
juce::String toJson(const Graph& graph);
bool saveGraph(const Graph& graph, const juce::File& file);

// Structural validation: missing nodes or pins, wrong direction, type
// mismatches, duplicate ids, inputs driven twice, unknown node types,
// missing required parameters, references to undefined functions and a
// missing output node. This is not FRust compilation: it does not check
// what a backend can lower.
std::vector<Diagnostic> validate(const Graph& graph);
std::vector<Diagnostic> validate(const Graph& graph, const DefinitionRegistry& registry);

// For editors that write the document themselves: copies into `written`
// every top-level section, and every key of a node (matched by id), that
// `previous` has and the editor does not manage, so a save does not drop
// what the editor does not understand. Keys in the managed lists are
// never copied, so a value the editor cleared stays cleared.
void carryForwardUnmanaged(juce::var& written, const juce::var& previous,
                           const juce::StringArray& managedRootKeys,
                           const juce::StringArray& managedNodeKeys);
}
