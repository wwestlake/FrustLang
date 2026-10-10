#pragma once

// Node definitions: the node types a FRust node graph may contain, with
// their typed pins and parameters. This is the one table the node editor,
// the graph validator and later the compiler consult; it was the editor's
// template list (NodeDesignerPanel::buildTemplates) before it moved here.
// Presentation (colours, layout) stays in the editor.

#include <juce_core/juce_core.h>

#include <vector>

namespace frust_node_graph
{
enum class PinFlow { Data, Exec, Stream, Resource };

juce::String flowName(PinFlow flow);
PinFlow flowFromName(const juce::String& name);

struct PinDefinition
{
    juce::String name;
    juce::String type;
    PinFlow flow = PinFlow::Data;
};

// A value a node carries besides its wired inputs, such as a literal's
// value or a transition's event. `required` parameters must be present and
// non-empty for the graph to be structurally valid.
struct ParameterDefinition
{
    juce::String name;
    juce::String type;
    bool required = false;
};

struct NodeDefinition
{
    juce::String type;
    juce::String title;
    juce::String category;
    std::vector<PinDefinition> inputs;
    std::vector<PinDefinition> outputs;
    std::vector<ParameterDefinition> parameters;
    int defaultLiteral = 0;
    // Inputs beyond the listed ones are allowed, named arg1, arg2, ... of
    // type any (an extern call takes any number of arguments).
    bool variadicInputs = false;
    // Exec inputs that accept several wires (a state entered by several
    // transitions); every other input takes at most one.
    bool multiDriveExecInputs = false;
    // Listed in the editor palette. Compiler-format types the editor can
    // display but does not offer for authoring are not.
    bool inPalette = true;
    // For call_function:<id> definitions, the function they call.
    juce::String functionRef;

    const PinDefinition* findInput(const juce::String& name) const;
    const PinDefinition* findOutput(const juce::String& name) const;
    int inputIndex(const juce::String& name) const;
    int outputIndex(const juce::String& name) const;
    // Name of input `index`, including variadic arguments; empty if none.
    juce::String inputName(int index) const;
};

// The data and exec wiring rule the editor has always applied: same flow,
// and the same type unless either side is `any`.
bool pinsCompatible(const PinDefinition& from, const PinDefinition& to);

// The built-in node types, in palette order.
const std::vector<NodeDefinition>& builtinDefinitions();

// The schemaVersion 1 compiler format predates the editor and gives one
// type a different shape: its `print` takes a single data value and has no
// exec pins. Returns that shape, or nullptr when the type is unchanged.
const NodeDefinition* schemaV1Definition(const juce::String& type);

// A function a graph can call through a call_function node: one of its own
// subgraphs or an external function named on the node.
struct FunctionSignature
{
    juce::String id;
    juce::String name;
    juce::String namespaceName;
    juce::String kind = "pure";            // pure / callable / macro
    juce::String accessibility = "package";
    std::vector<PinDefinition> inputs;
    std::vector<PinDefinition> outputs;
};

// The definition of the node that calls `fn` (type call_function:<id>):
// callable functions get exec pins in front of their data pins.
NodeDefinition callNodeDefinition(const FunctionSignature& fn);

class DefinitionRegistry
{
public:
    DefinitionRegistry();   // the built-in definitions

    const NodeDefinition* find(const juce::String& type) const;
    const std::vector<NodeDefinition>& all() const { return definitions; }
    // Adds or replaces the definition with the same type.
    void add(NodeDefinition definition);

private:
    std::vector<NodeDefinition> definitions;
};
}
