// frust_node_graph_inspect <schematic.json> [--resave <out.json>]
//
// Loads a node schematic without any editor, validates it and prints its
// interface, nodes and connections. With --resave it writes the document
// back through the graph model (current schema version).

#include <frust_node_graph/NodeGraph.h>

#include <cstdio>

using namespace frust_node_graph;

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: frust_node_graph_inspect <schematic.json> [--resave <out.json>]\n");
        return 2;
    }
    const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1]));
    const auto loaded = loadGraph(file);
    for (const auto& d : loaded.diagnostics)
        std::printf("load: %s\n", describe(d).toRawUTF8());
    if (!loaded.ok)
        return 1;

    const auto& g = loaded.graph;
    std::printf("%s: schemaVersion %d, %s, '%s'\n", file.getFileName().toRawUTF8(), g.schemaVersion, g.diagramType.toRawUTF8(), g.name.toRawUTF8());
    for (const auto& p : g.interface.inputs)
        std::printf("  input  %s: %s\n", p.name.toRawUTF8(), p.type.toRawUTF8());
    if (g.interface.output.isNotEmpty())
        std::printf("  output %s\n", g.interface.output.toRawUTF8());
    for (const auto& s : g.subgraphs)
        std::printf("  function %s (%s, %d in, %d out)\n", s.signature.id.toRawUTF8(), s.signature.kind.toRawUTF8(),
                    (int)s.signature.inputs.size(), (int)s.signature.outputs.size());

    const auto registry = g.definitions();
    std::printf("nodes (%d):\n", (int)g.nodes.size());
    for (const auto& n : g.nodes)
    {
        NodeDefinition d;
        const bool known = g.resolveDefinition(n, registry, d);
        juce::StringArray params;
        for (const auto& p : n.parameters)
            params.add(p.name.toString() + "=" + (p.value.isObject() || p.value.isArray() ? juce::JSON::toString(p.value, true) : p.value.toString()));
        std::printf("  %-18s %-22s %s%s\n", n.id.toRawUTF8(), n.type.toRawUTF8(), known ? "" : "(unknown type) ",
                    params.joinIntoString(", ").toRawUTF8());
    }
    std::printf("connections (%d):\n", (int)g.connections.size());
    for (const auto& c : g.connections)
        std::printf("  %s%s.%s -> %s.%s\n", c.from.graphInput ? "input " : "", c.from.node.toRawUTF8(), c.from.pin.toRawUTF8(),
                    c.to.node.toRawUTF8(), c.to.pin.toRawUTF8());

    const auto problems = validate(g);
    int errors = 0;
    for (const auto& d : problems)
    {
        errors += d.isError() ? 1 : 0;
        std::printf("validate: %s\n", describe(d).toRawUTF8());
    }
    std::printf("%d error(s), %d warning(s)\n", errors, (int)problems.size() - errors);

    if (argc >= 4 && juce::String(argv[2]) == "--resave")
    {
        const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[3]));
        if (!saveGraph(g, out))
        {
            std::printf("could not write %s\n", out.getFullPathName().toRawUTF8());
            return 1;
        }
        std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
    }
    return errors == 0 ? 0 : 1;
}
