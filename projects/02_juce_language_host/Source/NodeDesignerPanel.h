#pragma once

#include <JuceHeader.h>

#include <CompilerApi.h>
#include <node_compiler/NodeCompiler.h>

#include <memory>
#include <vector>

class NodeDesignerPanel : public juce::Component,
                          public juce::DragAndDropContainer
{
public:
    NodeDesignerPanel();
    explicit NodeDesignerPanel(const juce::File& fileToOpen);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void loadGraphFile(const juce::File& file);

    enum class PinFlow { Data, Exec, Stream, Resource };

    struct Pin
    {
        juce::String name;
        juce::String type;
        PinFlow flow = PinFlow::Data;
        bool isInput = true;
    };

private:
    struct GraphNode
    {
        int uid = 0;
        juce::String id;
        juce::String type;
        juce::String title;
        juce::String category;
        juce::Colour colour;
        float x = 0.0f;
        float y = 0.0f;
        int literalValue = 0;
        juce::String textValue;
        juce::String accessibility = "package";
        juce::String entryAction;
        juce::String updateAction;
        juce::String exitAction;
        juce::String eventName;
        juce::String payloadType;
        juce::String guardExpression;
        juce::String transitionAction;
        juce::String machineRef;
        bool initial = false;
        bool terminal = false;
        bool breakpoint = false;
        bool watched = false;
        std::vector<Pin> inputs;
        std::vector<Pin> outputs;
        std::vector<juce::String> inputDefaults;
        juce::String functionRef;
    };

    struct Connection
    {
        int fromNode = 0;
        int fromPin = 0;
        int toNode = 0;
        int toPin = 0;
    };

    struct NodeTemplate
    {
        juce::String type;
        juce::String title;
        juce::String category;
        juce::Colour colour;
        int defaultLiteral = 0;
        std::vector<Pin> inputs;
        std::vector<Pin> outputs;
        juce::String functionRef;
    };

    struct VariableDef
    {
        juce::String name;
        juce::String type;
        juce::String access;
        juce::String defaultValue;
    };

    struct TypeField
    {
        juce::String name;
        juce::String type;
    };

    struct TypeVariant
    {
        juce::String name;
        std::vector<TypeField> fields;
        int intValue = 0;
    };

    struct TypeDef
    {
        juce::String name;
        juce::String kind;
        juce::String targetFallback;
        std::vector<TypeField> fields;
        std::vector<TypeVariant> variants;
    };

    struct FunctionPort
    {
        juce::String name;
        juce::String type;
    };

    struct FunctionDef
    {
        juce::String id;
        juce::String name;
        juce::String namespaceName;
        juce::String accessibility;
        juce::String kind;
        juce::String owningPod;
        juce::String owningVersion;
        std::vector<FunctionPort> inputs;
        std::vector<FunctionPort> outputs;
    };

    struct ResourceDef
    {
        juce::String id;
        juce::String name;
        juce::String kind;
        juce::String accessibility;
    };

    struct StateDef
    {
        juce::String id;
        juce::String name;
        juce::String accessibility;
        juce::String entryAction;
        juce::String updateAction;
        juce::String exitAction;
        float x = 0.0f;
        float y = 0.0f;
        bool initial = false;
        bool terminal = false;
    };

    struct EventDef
    {
        juce::String id;
        juce::String name;
        juce::String payloadType;
    };

    struct TransitionDef
    {
        juce::String id;
        juce::String from;
        juce::String to;
        juce::String event;
        juce::String guard;
        juce::String action;
    };

    class PalettePanel;
    class GraphCanvas;
    class InspectorPanel;
    class VariablesPanel;
    class TypesPanel;

    static std::vector<NodeTemplate> buildTemplates();
    static juce::String quoted(const juce::String& text);

    GraphNode* findNode(int uid);
    const GraphNode* findNode(int uid) const;
    const NodeTemplate* findTemplate(const juce::String& type) const;
    const FunctionDef* findFunction(const juce::String& id) const;
    void refreshTemplatesWithFunctions();
    GraphNode& addNode(const juce::String& type, juce::Point<float> world);
    GraphNode& addRerouteNode(const Connection& connection, juce::Point<float> world);
    void removeNode(int uid);
    void connectPins(int fromNode, int fromPin, int toNode, int toPin);
    const Connection* connectionToInput(int nodeUid, int inputPin) const;
    bool pinsCompatible(const Pin& from, const Pin& to) const;
    bool isCompileableFrustDataNode(const GraphNode& node) const;
    bool hasExecutableGraph() const;
    juce::String validateFrustCompileable() const;
    const GraphNode* resolveRerouteUpstream(int nodeUid, int inputPin) const;
    const GraphNode* resolveRerouteDownstream(int nodeUid, int outputPin) const;
    juce::String validateStateMachine() const;
    juce::String buildExecutableFrustSource() const;
    juce::String expressionForInput(const GraphNode& node, int inputIndex) const;
    int nextUid();
    juce::String uniqueNodeId(const juce::String& base) const;

    void initializeUntitled();
    void loadFromJson(const juce::String& text);
    juce::String buildSchematicJson(bool includeRoutingNodes = true) const;
    juce::String buildCompilerJson(bool includeRoutingNodes) const;
    juce::String generatedSourcePathLabel() const;
    juce::File generatedSourceCacheFile() const;
    juce::String selectedDiagramType() const;
    juce::String selectedTarget() const;
    juce::String selectedFrustProjectType() const;
    juce::String outputNodeId() const;
    void initializeNodeGraph();
    void initializeStateMachine();
    void refreshProperties();
    void saveGraph();
    void compileGraph();
    void saveGeneratedSource();
    void setStatus(const juce::String& text, bool isError = false);

    std::vector<NodeTemplate> templates;
    std::vector<GraphNode> nodes;
    std::vector<Connection> connections;
    std::vector<VariableDef> variables;
    std::vector<TypeDef> types;
    std::vector<FunctionDef> functions;
    std::vector<ResourceDef> resources;
    std::vector<StateDef> states;
    std::vector<EventDef> events;
    std::vector<TransitionDef> transitions;
    std::vector<node_compiler::GeneratedFile> generatedFiles;
    int nextNodeUid = 1;
    int selectedNodeUid = 0;
    int selectedConnectionIndex = -1;
    juce::String generatedSource;
    juce::String diagramName = "Untitled Node Schematic";
    juce::String diagramType = "node_graph";
    juce::String frustProjectType = "bin";

    std::unique_ptr<PalettePanel> palette;
    std::unique_ptr<GraphCanvas> canvas;
    std::unique_ptr<InspectorPanel> inspector;
    std::unique_ptr<VariablesPanel> variablesPanel;
    std::unique_ptr<TypesPanel> typesPanel;

    juce::TextButton saveButton { "Save" };
    juce::TextButton compileButton { "Compile" };
    juce::TextButton saveSourceButton { "Export .fr" };
    juce::Label diagramTypeLabel { "DiagramTypeLabel", "Diagram" };
    juce::ComboBox diagramTypeSelector;
    juce::Label targetLabel { "TargetLabel", "Target" };
    juce::ComboBox targetSelector;
    juce::Label frustProjectTypeLabel { "FrustProjectTypeLabel", "Frate" };
    juce::ComboBox frustProjectTypeSelector;
    juce::Label titleLabel { "Title", "Untitled Node Schematic" };
    juce::Label propertiesTitle { "PropertiesTitle", "Properties" };
    juce::TextEditor statusView;

    juce::File currentGraphFile;
    juce::File currentSourceFile;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeDesignerPanel)
};
