#include "NodeDesignerPanel.h"

#include <node_compiler/NodeCompiler.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace
{
constexpr float nodeWidth = 174.0f;
constexpr float headerHeight = 25.0f;
constexpr float rowHeight = 22.0f;
constexpr float bottomPad = 9.0f;
constexpr float pinRadius = 5.0f;
constexpr float minZoom = 0.35f;
constexpr float maxZoom = 2.5f;
constexpr float rerouteSize = 24.0f;

juce::String propertyText(const juce::var& object, const juce::Identifier& property, const juce::String& fallback = {})
{
    if (!object.isObject()) return fallback;
    const auto value = object.getProperty(property, {});
    return value.isVoid() ? fallback : value.toString();
}

int propertyInt(const juce::var& object, const juce::Identifier& property, int fallback = 0)
{
    if (!object.isObject()) return fallback;
    const auto value = object.getProperty(property, {});
    return value.isVoid() ? fallback : (int)value;
}

float propertyFloat(const juce::var& object, const juce::Identifier& property, float fallback = 0.0f)
{
    if (!object.isObject()) return fallback;
    const auto value = object.getProperty(property, {});
    return value.isVoid() ? fallback : (float)(double)value;
}

juce::String baseIdForType(const juce::String& type)
{
    if (type == "reroute") return "route";
    if (type == "state_machine_instance") return "machine";
    if (type == "sm_state") return "state";
    if (type == "sm_transition") return "transition";
    if (type == "sm_event") return "event";
    if (type == "param_i64") return "x";
    if (type == "literal_i64") return "number";
    if (type == "literal_string") return "text";
    if (type == "add") return "add";
    if (type == "sub") return "sub";
    if (type == "mul") return "mul";
    if (type == "div") return "div";
    if (type == "mod") return "mod";
    if (type == "eq") return "equals";
    if (type == "lt") return "less";
    if (type == "gt") return "greater";
    if (type == "if") return "select";
    if (type.startsWith("call_function:")) return type.fromFirstOccurrenceOf(":", false, false);
    return type.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_").toLowerCase();
}

juce::Colour pinColour(const NodeDesignerPanel::Pin& pin)
{
    if (pin.flow == NodeDesignerPanel::PinFlow::Exec) return juce::Colours::white;
    if (pin.flow == NodeDesignerPanel::PinFlow::Stream) return juce::Colour(0xff9c8a5a);
    if (pin.flow == NodeDesignerPanel::PinFlow::Resource) return juce::Colour(0xff9c5a9c);
    if (pin.type == "bool") return juce::Colour(0xffd85a5a);
    if (pin.type == "string") return juce::Colour(0xffd85ad0);
    if (pin.type == "any") return juce::Colour(0xff9a9a9a);
    return juce::Colour(0xff5ad8a0);
}

juce::String pinListText(const std::vector<NodeDesignerPanel::Pin>& pins)
{
    juce::String text;
    for (const auto& pin : pins)
    {
        if (text.isNotEmpty()) text << ", ";
        text << pin.name << ":" << pin.type;
    }
    return text;
}

juce::String flowName(NodeDesignerPanel::PinFlow flow)
{
    switch (flow)
    {
        case NodeDesignerPanel::PinFlow::Data: return "data";
        case NodeDesignerPanel::PinFlow::Exec: return "exec";
        case NodeDesignerPanel::PinFlow::Stream: return "stream";
        case NodeDesignerPanel::PinFlow::Resource: return "resource";
    }
    return "data";
}

NodeDesignerPanel::PinFlow flowFromName(const juce::String& flow)
{
    if (flow == "exec") return NodeDesignerPanel::PinFlow::Exec;
    if (flow == "stream") return NodeDesignerPanel::PinFlow::Stream;
    if (flow == "resource") return NodeDesignerPanel::PinFlow::Resource;
    return NodeDesignerPanel::PinFlow::Data;
}
}

class NodeDesignerPanel::PalettePanel final : public juce::Component
{
public:
    explicit PalettePanel(NodeDesignerPanel& ownerIn)
        : owner(ownerIn)
    {
        title.setFont(juce::Font(15.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(title);

        filter.setTextToShowWhenEmpty("Filter nodes...", juce::Colour(0xff6b7a8c));
        filter.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff20262f));
        filter.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        filter.onTextChange = [this] { rebuildRows(); repaint(); };
        addAndMakeVisible(filter);

        rebuildRows();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        title.setBounds(b.removeFromTop(25).reduced(8, 0));
        filter.setBounds(b.removeFromTop(30).reduced(6, 3));
        listBounds = b;
    }

    void refreshRows()
    {
        rebuildRows();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff15181d));
        g.setColour(juce::Colour(0xff181c22));
        g.fillRect(listBounds);

        rowBounds.clear();
        int y = listBounds.getY() + 4;
        for (int i = 0; i < (int)rows.size(); ++i)
        {
            const auto h = rows[(size_t)i].header ? 25 : 31;
            auto rowArea = juce::Rectangle<int>(listBounds.getX() + 4, y, listBounds.getWidth() - 8, h);
            rowBounds.push_back(rowArea);
            paintRow(g, i, rowArea);
            y += h;
        }
    }

private:
    struct Row
    {
        bool header = false;
        juce::String category;
        int templateIndex = -1;
        int count = 0;
    };

    void rebuildRows()
    {
        rows.clear();
        const auto text = filter.getText().trim();
        std::vector<juce::String> categories;
        for (const auto& t : owner.templates)
            if (std::find(categories.begin(), categories.end(), t.category) == categories.end())
                categories.push_back(t.category);
        std::sort(categories.begin(), categories.end());

        for (const auto& category : categories)
        {
            std::vector<int> matches;
            for (int i = 0; i < (int)owner.templates.size(); ++i)
            {
                const auto& t = owner.templates[(size_t)i];
                if (t.category != category) continue;
                if (text.isEmpty() || t.title.containsIgnoreCase(text) || t.type.containsIgnoreCase(text))
                    matches.push_back(i);
            }
            if (matches.empty()) continue;

            rows.push_back({ true, category, -1, (int)matches.size() });
            const bool expanded = expandedCategories.count(category) != 0 || text.isNotEmpty();
            if (expanded)
                for (const auto i : matches)
                    rows.push_back({ false, category, i, 0 });
        }
    }

    void paintRow(juce::Graphics& g, int row, juce::Rectangle<int> bounds)
    {
        if (row < 0 || row >= (int)rows.size()) return;
        const auto& r = rows[(size_t)row];
        if (r.header)
        {
            g.setColour(juce::Colour(0xff20262f));
            g.fillRect(bounds);
            const bool expanded = expandedCategories.count(r.category) != 0 || filter.getText().trim().isNotEmpty();
            g.setColour(juce::Colour(0xff9aa8ba));
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawText((expanded ? "v " : "> ") + r.category + "  (" + juce::String(r.count) + ")",
                       bounds.reduced(7, 0), juce::Justification::centredLeft, true);
            return;
        }

        const auto& t = owner.templates[(size_t)r.templateIndex];
        if (row == hoverRow)
        {
            g.setColour(juce::Colour(0xff24303d));
            g.fillRoundedRectangle(bounds.toFloat().reduced(1.0f), 4.0f);
        }
        g.setColour(t.colour);
        g.fillRoundedRectangle((float)bounds.getX() + 7.0f, (float)bounds.getCentreY() - 5.0f, 10.0f, 10.0f, 3.0f);
        g.setColour(juce::Colour(0xffc7d2df));
        g.setFont(juce::Font(13.0f));
        g.drawText(t.title, bounds.withTrimmedLeft(24).withTrimmedBottom(12), juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff758294));
        g.setFont(juce::Font(10.5f));
        g.drawText(t.type, bounds.withTrimmedLeft(24).withTrimmedTop(14), juce::Justification::centredLeft, true);
    }

    int rowAt(juce::Point<int> position) const
    {
        for (int i = 0; i < (int)rowBounds.size(); ++i)
            if (rowBounds[(size_t)i].contains(position))
                return i;
        return -1;
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto row = rowAt(e.getPosition());
        if (row != hoverRow)
        {
            hoverRow = row;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoverRow = -1;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto row = rowAt(e.getPosition());
        if (row < 0 || row >= (int)rows.size()) return;
        const auto& r = rows[(size_t)row];
        if (r.header)
        {
            if (expandedCategories.count(r.category) == 0) expandedCategories.insert(r.category);
            else expandedCategories.erase(r.category);
            rebuildRows();
            repaint();
            return;
        }
        if (r.templateIndex < 0) return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            container->startDragging(owner.templates[(size_t)r.templateIndex].type, this);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const auto row = rowAt(e.getPosition());
        if (row < 0 || row >= (int)rows.size()) return;
        const auto& r = rows[(size_t)row];
        if (r.header || r.templateIndex < 0) return;
        owner.addNode(owner.templates[(size_t)r.templateIndex].type, { 120.0f, 120.0f });
        owner.repaint();
    }

    NodeDesignerPanel& owner;
    juce::Label title { {}, "Nodes" };
    juce::TextEditor filter;
    juce::Rectangle<int> listBounds;
    std::vector<Row> rows;
    std::vector<juce::Rectangle<int>> rowBounds;
    std::set<juce::String> expandedCategories { "Sources", "Math", "Logic", "Execution", "Functions", "Variables", "Types", "Streams", "Resources" };
    int hoverRow = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PalettePanel)
};

class NodeDesignerPanel::GraphCanvas final : public juce::Component,
                                            public juce::DragAndDropTarget
{
public:
    explicit GraphCanvas(NodeDesignerPanel& ownerIn) : owner(ownerIn)
    {
        setWantsKeyboardFocus(true);
    }

    juce::Point<float> worldToScreen(juce::Point<float> world) const
    {
        return { world.x * zoom + viewOffset.x, world.y * zoom + viewOffset.y };
    }

    juce::Point<float> screenToWorld(juce::Point<float> screen) const
    {
        return { (screen.x - viewOffset.x) / zoom, (screen.y - viewOffset.y) / zoom };
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff11151b));
        drawGrid(g);
        drawConnections(g);
        drawDraggedWire(g);
        drawNodes(g);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
        const auto pos = event.position;
        PinHit pin;
        if (hitTestPin(pos, pin))
        {
            draggingWire = true;
            wireStart = pin;
            wireDragPos = pos;
            repaint();
            return;
        }

        const int node = hitTestNode(pos);
        if (node != 0)
        {
            owner.selectedNodeUid = node;
            owner.selectedConnectionIndex = -1;
            draggingNode = true;
            dragNodeUid = node;
            dragStartMouseWorld = screenToWorld(pos);
            if (auto* n = owner.findNode(node))
                dragStartNodeWorld = { n->x, n->y };
            owner.refreshProperties();
            repaint();
            return;
        }

        const int conn = hitTestConnection(pos);
        if (conn >= 0)
        {
            owner.selectedNodeUid = 0;
            owner.selectedConnectionIndex = conn;
            owner.refreshProperties();
            repaint();
            return;
        }

        owner.selectedNodeUid = 0;
        owner.selectedConnectionIndex = -1;
        owner.refreshProperties();
        panning = true;
        panStartMouse = pos;
        panStartOffset = viewOffset;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (draggingWire)
        {
            wireDragPos = event.position;
            repaint();
            return;
        }
        if (draggingNode)
        {
            if (auto* node = owner.findNode(dragNodeUid))
            {
                const auto delta = screenToWorld(event.position) - dragStartMouseWorld;
                node->x = dragStartNodeWorld.x + delta.x;
                node->y = dragStartNodeWorld.y + delta.y;
                owner.refreshProperties();
                repaint();
            }
            return;
        }
        if (panning)
        {
            viewOffset = panStartOffset + (event.position - panStartMouse);
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (draggingWire)
        {
            draggingWire = false;
            PinHit target;
            if (hitTestPin(event.position, target) && target.nodeUid != 0)
            {
                PinHit output = wireStart.isInput ? target : wireStart;
                PinHit input = wireStart.isInput ? wireStart : target;
                if (!output.isInput && input.isInput && output.nodeUid != input.nodeUid)
                    owner.connectPins(output.nodeUid, output.pinIndex, input.nodeUid, input.pinIndex);
            }
            repaint();
            return;
        }
        draggingNode = false;
        panning = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const int conn = hitTestConnection(event.position);
        if (conn >= 0 && conn < (int)owner.connections.size())
        {
            const auto connection = owner.connections[(size_t)conn];
            auto& reroute = owner.addRerouteNode(connection, screenToWorld(event.position) - juce::Point<float> { rerouteSize * 0.5f, rerouteSize * 0.5f });
            owner.connections.erase(owner.connections.begin() + conn);
            owner.connections.push_back({ connection.fromNode, connection.fromPin, reroute.uid, 0 });
            owner.connections.push_back({ reroute.uid, 0, connection.toNode, connection.toPin });
            owner.selectedNodeUid = reroute.uid;
            owner.selectedConnectionIndex = -1;
            owner.refreshProperties();
            repaint();
            return;
        }

        const int node = hitTestNode(event.position);
        if (auto* n = owner.findNode(node))
        {
            if (n->type == "reroute")
                return;
            n->breakpoint = !n->breakpoint;
            owner.refreshProperties();
            repaint();
        }
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        const auto before = screenToWorld(event.position);
        const float factor = wheel.deltaY > 0.0f ? 1.1f : (1.0f / 1.1f);
        zoom = std::clamp(zoom * factor, minZoom, maxZoom);
        viewOffset += event.position - worldToScreen(before);
        repaint();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key != juce::KeyPress::deleteKey && key != juce::KeyPress::backspaceKey)
            return false;
        if (owner.selectedConnectionIndex >= 0 && owner.selectedConnectionIndex < (int)owner.connections.size())
        {
            owner.connections.erase(owner.connections.begin() + owner.selectedConnectionIndex);
            owner.selectedConnectionIndex = -1;
            owner.refreshProperties();
            repaint();
            return true;
        }
        if (owner.selectedNodeUid != 0)
        {
            owner.removeNode(owner.selectedNodeUid);
            owner.selectedNodeUid = 0;
            owner.refreshProperties();
            repaint();
            return true;
        }
        return false;
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return details.description.isString();
    }

    void itemDropped(const SourceDetails& details) override
    {
        const auto type = details.description.toString();
        owner.addNode(type, screenToWorld(details.localPosition.toFloat()));
        repaint();
    }

private:
    struct PinHit
    {
        int nodeUid = 0;
        int pinIndex = -1;
        bool isInput = false;
    };

    float nodeHeight(const GraphNode& node) const
    {
        if (node.type == "reroute") return rerouteSize;
        return headerHeight + (float)std::max(node.inputs.size(), node.outputs.size()) * rowHeight + bottomPad;
    }

    juce::Rectangle<float> nodeBounds(const GraphNode& node) const
    {
        const auto topLeft = worldToScreen({ node.x, node.y });
        if (node.type == "reroute")
            return { topLeft.x, topLeft.y, rerouteSize * zoom, rerouteSize * zoom };
        return { topLeft.x, topLeft.y, nodeWidth * zoom, nodeHeight(node) * zoom };
    }

    juce::Point<float> pinPosition(const GraphNode& node, const Pin& pin, int index) const
    {
        if (node.type == "reroute")
        {
            const float x = pin.isInput ? 0.0f : rerouteSize;
            return worldToScreen({ node.x + x, node.y + rerouteSize * 0.5f });
        }
        const float x = pin.isInput ? 0.0f : nodeWidth;
        const float y = headerHeight + ((float)index + 0.5f) * rowHeight;
        return worldToScreen({ node.x + x, node.y + y });
    }

    int hitTestNode(juce::Point<float> pos) const
    {
        for (auto it = owner.nodes.rbegin(); it != owner.nodes.rend(); ++it)
            if (nodeBounds(*it).contains(pos))
                return it->uid;
        return 0;
    }

    bool hitTestPin(juce::Point<float> pos, PinHit& hit) const
    {
        const float radius = pinRadius * zoom + 6.0f;
        for (const auto& node : owner.nodes)
        {
            for (int i = 0; i < (int)node.inputs.size(); ++i)
            {
                if (pinPosition(node, node.inputs[(size_t)i], i).getDistanceFrom(pos) <= radius)
                {
                    hit = { node.uid, i, true };
                    return true;
                }
            }
            for (int i = 0; i < (int)node.outputs.size(); ++i)
            {
                if (pinPosition(node, node.outputs[(size_t)i], i).getDistanceFrom(pos) <= radius)
                {
                    hit = { node.uid, i, false };
                    return true;
                }
            }
        }
        return false;
    }

    bool connectionEndpoints(const Connection& c, juce::Point<float>& from, juce::Point<float>& to) const
    {
        const auto* fromNode = owner.findNode(c.fromNode);
        const auto* toNode = owner.findNode(c.toNode);
        if (fromNode == nullptr || toNode == nullptr) return false;
        if (c.fromPin < 0 || c.fromPin >= (int)fromNode->outputs.size()) return false;
        if (c.toPin < 0 || c.toPin >= (int)toNode->inputs.size()) return false;
        from = pinPosition(*fromNode, fromNode->outputs[(size_t)c.fromPin], c.fromPin);
        to = pinPosition(*toNode, toNode->inputs[(size_t)c.toPin], c.toPin);
        return true;
    }

    int hitTestConnection(juce::Point<float> pos) const
    {
        constexpr float hitDistance = 6.0f;
        constexpr int samples = 24;
        int best = -1;
        float bestDistance = hitDistance;
        for (int i = 0; i < (int)owner.connections.size(); ++i)
        {
            juce::Point<float> from, to;
            if (!connectionEndpoints(owner.connections[(size_t)i], from, to)) continue;
            const float dx = std::max(30.0f, std::abs(to.x - from.x) * 0.5f);
            const juce::Point<float> c1 { from.x + dx, from.y };
            const juce::Point<float> c2 { to.x - dx, to.y };
            for (int s = 0; s <= samples; ++s)
            {
                const float t = (float)s / (float)samples;
                const float u = 1.0f - t;
                const juce::Point<float> p {
                    u * u * u * from.x + 3.0f * u * u * t * c1.x + 3.0f * u * t * t * c2.x + t * t * t * to.x,
                    u * u * u * from.y + 3.0f * u * u * t * c1.y + 3.0f * u * t * t * c2.y + t * t * t * to.y
                };
                const auto d = p.getDistanceFrom(pos);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = i;
                }
            }
        }
        return best;
    }

    void drawGrid(juce::Graphics& g)
    {
        const float minor = 32.0f * zoom;
        const float major = minor * 4.0f;
        if (major > 4.0f)
        {
            g.setColour(juce::Colour(0xff26313f));
            for (float x = std::fmod(viewOffset.x, major); x < (float)getWidth(); x += major)
                g.drawVerticalLine((int)x, 0.0f, (float)getHeight());
            for (float y = std::fmod(viewOffset.y, major); y < (float)getHeight(); y += major)
                g.drawHorizontalLine((int)y, 0.0f, (float)getWidth());
        }
        if (minor > 16.0f)
        {
            g.setColour(juce::Colour(0xff1a212a));
            for (float x = std::fmod(viewOffset.x, minor); x < (float)getWidth(); x += minor)
                g.drawVerticalLine((int)x, 0.0f, (float)getHeight());
            for (float y = std::fmod(viewOffset.y, minor); y < (float)getHeight(); y += minor)
                g.drawHorizontalLine((int)y, 0.0f, (float)getWidth());
        }
    }

    const StateDef* findState(const juce::String& id) const
    {
        auto it = std::find_if(owner.states.begin(), owner.states.end(), [&id](const StateDef& s) { return s.id == id; });
        return it == owner.states.end() ? nullptr : &*it;
    }

    juce::Rectangle<float> stateBounds(const StateDef& state) const
    {
        const auto topLeft = worldToScreen({ state.x, state.y });
        return { topLeft.x, topLeft.y, 190.0f * zoom, 82.0f * zoom };
    }

    void drawStateMachine(juce::Graphics& g)
    {
        for (const auto& t : owner.transitions)
        {
            const auto* from = findState(t.from);
            const auto* to = findState(t.to);
            if (from == nullptr || to == nullptr) continue;

            const auto fromBounds = stateBounds(*from);
            const auto toBounds = stateBounds(*to);
            const auto start = fromBounds.getCentre();
            const auto end = toBounds.getCentre();
            drawStateTransition(g, start, end, t.event);
        }

        for (const auto& state : owner.states)
            drawState(g, state);

        if (owner.states.empty())
        {
            g.setColour(juce::Colour(0xff9aa8ba));
            g.setFont(juce::Font(15.0f));
            g.drawFittedText("State Machine diagram: add states, events, and transitions.",
                             getLocalBounds().reduced(24), juce::Justification::centred, 3);
        }
    }

    void drawStateTransition(juce::Graphics& g, juce::Point<float> start, juce::Point<float> end, const juce::String& label)
    {
        juce::Path p;
        p.startNewSubPath(start);
        const auto mid = (start + end) * 0.5f;
        const juce::Point<float> normal { -(end.y - start.y) * 0.12f, (end.x - start.x) * 0.12f };
        p.quadraticTo(mid + normal, end);
        g.setColour(juce::Colour(0xff6cb6ff));
        g.strokePath(p, juce::PathStrokeType(2.0f));

        const auto dir = (end - start);
        const auto len = std::max(1.0f, std::sqrt(dir.x * dir.x + dir.y * dir.y));
        const juce::Point<float> unit { dir.x / len, dir.y / len };
        const juce::Point<float> tip = end - unit * 96.0f * zoom;
        juce::Path arrow;
        arrow.startNewSubPath(tip);
        arrow.lineTo(tip - unit * 10.0f + juce::Point<float> { -unit.y, unit.x } * 6.0f);
        arrow.lineTo(tip - unit * 10.0f + juce::Point<float> { unit.y, -unit.x } * 6.0f);
        arrow.closeSubPath();
        g.fillPath(arrow);

        if (label.isNotEmpty())
        {
            g.setColour(juce::Colour(0xffdce9ee));
            g.setFont(juce::Font(11.5f));
            g.drawText(label, juce::Rectangle<float>(mid.x - 70.0f, mid.y - 13.0f, 140.0f, 22.0f),
                       juce::Justification::centred, true);
        }
    }

    void drawState(juce::Graphics& g, const StateDef& state)
    {
        auto bounds = stateBounds(state);
        g.setColour(state.initial ? juce::Colour(0xff21412f) : juce::Colour(0xff18232d));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(state.initial ? juce::Colour(0xff67d391) : juce::Colour(0xff4b8fc8));
        g.drawRoundedRectangle(bounds, 8.0f, state.initial ? 2.5f : 1.5f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(state.name, bounds.reduced(12.0f, 8.0f).removeFromTop(22.0f), juce::Justification::centredLeft, true);

        g.setColour(juce::Colour(0xff9aa8ba));
        g.setFont(juce::Font(11.0f));
        juce::String details;
        if (state.initial) details << "initial";
        if (state.terminal) details << (details.isNotEmpty() ? "  |  " : "") << "terminal";
        if (state.entryAction.isNotEmpty()) details << (details.isNotEmpty() ? "  |  " : "") << "entry: " << state.entryAction;
        if (details.isEmpty()) details = state.id;
        g.drawFittedText(details, bounds.reduced(12.0f, 34.0f).toNearestInt(), juce::Justification::topLeft, 2);
    }

    void drawWire(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour, float thickness = 2.0f)
    {
        juce::Path path;
        path.startNewSubPath(from);
        const float dx = std::max(30.0f, std::abs(to.x - from.x) * 0.5f);
        path.cubicTo(from.x + dx, from.y, to.x - dx, to.y, to.x, to.y);
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(thickness));
    }

    void drawPin(juce::Graphics& g, const Pin& pin, juce::Point<float> pos)
    {
        const auto size = pinRadius * 2.0f * zoom;
        const auto r = juce::Rectangle<float>(pos.x - pinRadius * zoom, pos.y - pinRadius * zoom, size, size);
        g.setColour(pinColour(pin));
        if (pin.flow == PinFlow::Exec)
            g.fillRect(r.expanded(1.0f));
        else if (pin.flow == PinFlow::Stream)
            g.fillRoundedRectangle(r.expanded(1.0f, 0.0f), 2.0f);
        else if (pin.flow == PinFlow::Resource)
        {
            juce::Path diamond;
            diamond.startNewSubPath(pos.x, r.getY() - 1.0f);
            diamond.lineTo(r.getRight() + 1.0f, pos.y);
            diamond.lineTo(pos.x, r.getBottom() + 1.0f);
            diamond.lineTo(r.getX() - 1.0f, pos.y);
            diamond.closeSubPath();
            g.fillPath(diamond);
        }
        else
            g.fillEllipse(r);
    }

    void drawConnections(juce::Graphics& g)
    {
        for (int i = 0; i < (int)owner.connections.size(); ++i)
        {
            const auto& c = owner.connections[(size_t)i];
            juce::Point<float> from, to;
            if (!connectionEndpoints(c, from, to)) continue;
            const auto* n = owner.findNode(c.fromNode);
            const auto colour = n != nullptr && c.fromPin < (int)n->outputs.size()
                ? pinColour(n->outputs[(size_t)c.fromPin])
                : juce::Colours::grey;
            drawWire(g, from, to, i == owner.selectedConnectionIndex ? juce::Colours::white : colour,
                     i == owner.selectedConnectionIndex ? 3.5f : 2.0f);
        }
    }

    void drawDraggedWire(juce::Graphics& g)
    {
        if (!draggingWire) return;
        const auto* node = owner.findNode(wireStart.nodeUid);
        if (node == nullptr) return;
        if (wireStart.isInput)
        {
            if (wireStart.pinIndex >= 0 && wireStart.pinIndex < (int)node->inputs.size())
                drawWire(g, wireDragPos, pinPosition(*node, node->inputs[(size_t)wireStart.pinIndex], wireStart.pinIndex), juce::Colours::white);
        }
        else
        {
            if (wireStart.pinIndex >= 0 && wireStart.pinIndex < (int)node->outputs.size())
                drawWire(g, pinPosition(*node, node->outputs[(size_t)wireStart.pinIndex], wireStart.pinIndex), wireDragPos, juce::Colours::white);
        }
    }

    void drawNodes(juce::Graphics& g)
    {
        for (const auto& node : owner.nodes)
        {
            const auto bounds = nodeBounds(node);
            const bool selected = node.uid == owner.selectedNodeUid;
            if (node.type == "reroute")
            {
                const auto centre = bounds.getCentre();
                const auto radius = juce::jmax(5.0f, 7.0f * zoom);
                const auto dot = juce::Rectangle<float>(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
                const auto colour = !node.outputs.empty() ? pinColour(node.outputs.front()) : juce::Colour(0xff7fffd4);
                g.setColour(juce::Colour(0xff11151b));
                g.fillEllipse(dot.expanded(3.0f * zoom));
                g.setColour(colour);
                g.fillEllipse(dot);
                g.setColour(selected ? juce::Colours::white : juce::Colour(0xff384354));
                g.drawEllipse(dot.expanded(selected ? 3.0f : 2.0f), selected ? 2.0f : 1.0f);
                continue;
            }
            g.setColour(juce::Colour(0xff1d2530));
            g.fillRoundedRectangle(bounds, 6.0f);

            auto header = bounds.withHeight(headerHeight * zoom);
            g.setColour(node.colour);
            g.fillRoundedRectangle(header, 6.0f);
            g.fillRect(header.withTop(header.getBottom() - 6.0f * zoom));

            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(std::max(10.0f, 13.0f * zoom), juce::Font::bold));
            const auto displayTitle = ((node.type.startsWith("sm_") || node.type == "state_machine_instance") && node.textValue.isNotEmpty())
                ? node.textValue
                : node.title;
            g.drawText(displayTitle, header.reduced(7.0f * zoom, 0.0f), juce::Justification::centredLeft, true);

            g.setColour(selected ? juce::Colours::white : juce::Colour(0xff384354));
            g.drawRoundedRectangle(bounds, 6.0f, selected ? 2.0f : 1.0f);

            if (node.breakpoint)
            {
                const auto dot = juce::Rectangle<float>(bounds.getX() - 7.0f, bounds.getY() - 7.0f, 14.0f, 14.0f);
                g.setColour(juce::Colour(0xffff3b30));
                g.fillEllipse(dot);
                g.setColour(juce::Colour(0xfff8d7da));
                g.drawEllipse(dot, 1.0f);
            }

            if (node.watched)
            {
                const auto badge = juce::Rectangle<float>(bounds.getRight() - 24.0f, bounds.getY() - 5.0f, 24.0f, 15.0f);
                g.setColour(juce::Colour(0xff101820));
                g.fillRoundedRectangle(badge, 5.0f);
                g.setColour(juce::Colour(0xff76d7ff));
                g.drawRoundedRectangle(badge, 5.0f, 1.0f);
                g.setFont(juce::Font(10.0f, juce::Font::bold));
                g.drawText("W", badge, juce::Justification::centred);
            }

            g.setFont(juce::Font(std::max(9.0f, 11.0f * zoom)));
            if (node.type == "sm_state")
            {
                juce::String badges;
                if (node.initial) badges << "initial";
                if (node.terminal) badges << (badges.isNotEmpty() ? " | " : "") << "terminal";
                if (node.entryAction.isNotEmpty()) badges << (badges.isNotEmpty() ? " | " : "") << "entry";
                if (badges.isNotEmpty())
                {
                    g.setColour(juce::Colour(0xff9aa8ba));
                    g.drawText(badges, bounds.reduced(9.0f * zoom, 29.0f * zoom),
                               juce::Justification::topLeft, true);
                }
            }
            else if (node.type == "sm_transition")
            {
                g.setColour(juce::Colour(0xffdce9ee));
                g.drawText("on " + (node.eventName.isNotEmpty() ? node.eventName : juce::String("event")),
                           bounds.reduced(9.0f * zoom, 29.0f * zoom), juce::Justification::topLeft, true);
            }
            for (int i = 0; i < (int)node.inputs.size(); ++i)
            {
                const auto& pin = node.inputs[(size_t)i];
                const auto pos = pinPosition(node, pin, i);
                drawPin(g, pin, pos);
                g.setColour(juce::Colours::lightgrey);
                g.drawText(pin.name, juce::Rectangle<float>(pos.x + 8.0f * zoom, pos.y - rowHeight * zoom * 0.5f,
                                                            nodeWidth * 0.5f * zoom, rowHeight * zoom),
                           juce::Justification::centredLeft, true);
            }
            for (int i = 0; i < (int)node.outputs.size(); ++i)
            {
                const auto& pin = node.outputs[(size_t)i];
                const auto pos = pinPosition(node, pin, i);
                drawPin(g, pin, pos);
                g.setColour(juce::Colours::lightgrey);
                g.drawText(pin.name, juce::Rectangle<float>(pos.x - (nodeWidth * 0.5f + 8.0f) * zoom, pos.y - rowHeight * zoom * 0.5f,
                                                            nodeWidth * 0.5f * zoom, rowHeight * zoom),
                           juce::Justification::centredRight, true);
            }
        }
    }

    NodeDesignerPanel& owner;
    juce::Point<float> viewOffset { 70.0f, 70.0f };
    float zoom = 1.0f;
    bool panning = false;
    juce::Point<float> panStartMouse;
    juce::Point<float> panStartOffset;
    bool draggingNode = false;
    int dragNodeUid = 0;
    juce::Point<float> dragStartMouseWorld;
    juce::Point<float> dragStartNodeWorld;
    bool draggingWire = false;
    PinHit wireStart;
    juce::Point<float> wireDragPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphCanvas)
};

class NodeDesignerPanel::InspectorPanel final : public juce::Component
{
public:
    explicit InspectorPanel(NodeDesignerPanel& ownerIn) : owner(ownerIn)
    {
        addAndMakeVisible(diagramName);
        addAndMakeVisible(integerValue);
        addAndMakeVisible(stringValue);
        addAndMakeVisible(nodeName);
        addAndMakeVisible(accessibility);
        addAndMakeVisible(entryAction);
        addAndMakeVisible(updateAction);
        addAndMakeVisible(exitAction);
        addAndMakeVisible(eventName);
        addAndMakeVisible(payloadType);
        addAndMakeVisible(guardExpression);
        addAndMakeVisible(transitionAction);
        addAndMakeVisible(machineRef);
        addAndMakeVisible(initialToggle);
        addAndMakeVisible(terminalToggle);

        configureEditor(diagramName);
        configureEditor(integerValue);
        configureEditor(stringValue);
        for (auto* editor : { &nodeName, &accessibility, &entryAction, &updateAction, &exitAction,
                              &eventName, &payloadType, &guardExpression, &transitionAction, &machineRef })
            configureEditor(*editor);

        initialToggle.setButtonText("Initial state");
        terminalToggle.setButtonText("Terminal state");
        initialToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));
        terminalToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffdce9ee));

        for (int i = 0; i < maxInputEditors; ++i)
        {
            auto* editor = new juce::TextEditor();
            configureEditor(*editor);
            editor->setVisible(false);
            editor->onTextChange = [this, i] {
                if (auto* node = owner.findNode(owner.selectedNodeUid))
                {
                    if (i >= 0 && i < (int)node->inputs.size() && owner.connectionToInput(node->uid, i) == nullptr)
                    {
                        while ((int)node->inputDefaults.size() <= i)
                            node->inputDefaults.push_back(defaultValueFor(node->inputs[(size_t)node->inputDefaults.size()]));
                        node->inputDefaults[(size_t)i] = inputEditors[i]->getText();
                    }
                }
            };
            addAndMakeVisible(editor);
            inputEditors.add(editor);
        }

        diagramName.onTextChange = [this] {
            owner.diagramName = diagramName.getText().trim().isEmpty() ? juce::String("Untitled Node Schematic") : diagramName.getText();
            owner.titleLabel.setText(owner.diagramName, juce::dontSendNotification);
        };
        integerValue.onTextChange = [this] {
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && node->type == "literal_i64")
            {
                node->literalValue = integerValue.getText().getIntValue();
                if (owner.canvas != nullptr) owner.canvas->repaint();
            }
        };
        stringValue.onTextChange = [this] {
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && node->type == "literal_string")
            {
                node->textValue = stringValue.getText();
                if (owner.canvas != nullptr) owner.canvas->repaint();
            }
        };
        nodeName.onTextChange = [this] {
            if (syncingEditors) return;
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && (node->type.startsWith("sm_") || node->type == "state_machine_instance"))
            {
                auto name = nodeName.getText().trim();
                if (name.isEmpty()) name = node->title;
                node->textValue = name;
                node->id = name.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_").toLowerCase();
                if (node->id.isEmpty()) node->id = uniqueFallbackId(*node);
                if (owner.canvas != nullptr) owner.canvas->repaint();
            }
        };
        accessibility.onTextChange = [this] {
            if (syncingEditors) return;
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && node->type == "sm_state")
                node->accessibility = accessibility.getText().trim();
        };
        entryAction.onTextChange = [this] { updateSelectedStateText(entryAction, &GraphNode::entryAction); };
        updateAction.onTextChange = [this] { updateSelectedStateText(updateAction, &GraphNode::updateAction); };
        exitAction.onTextChange = [this] { updateSelectedStateText(exitAction, &GraphNode::exitAction); };
        eventName.onTextChange = [this] {
            if (syncingEditors) return;
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && (node->type == "sm_transition" || node->type == "sm_event"))
            {
                node->eventName = eventName.getText().trim();
                if (node->eventName.isEmpty()) node->eventName = node->id;
                if (owner.canvas != nullptr) owner.canvas->repaint();
            }
        };
        payloadType.onTextChange = [this] {
            if (syncingEditors) return;
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && node->type == "sm_event")
                node->payloadType = payloadType.getText().trim();
        };
        guardExpression.onTextChange = [this] {
            if (syncingEditors) return;
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && node->type == "sm_transition")
                node->guardExpression = guardExpression.getText().trim();
        };
        transitionAction.onTextChange = [this] {
            if (syncingEditors) return;
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && node->type == "sm_transition")
                node->transitionAction = transitionAction.getText().trim();
        };
        machineRef.onTextChange = [this] {
            if (syncingEditors) return;
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && node->type == "state_machine_instance")
                node->machineRef = machineRef.getText().trim();
        };
        initialToggle.onClick = [this] {
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && node->type == "sm_state")
            {
                node->initial = initialToggle.getToggleState();
                if (node->initial)
                    for (auto& other : owner.nodes)
                        if (other.uid != node->uid && other.type == "sm_state")
                            other.initial = false;
                if (owner.canvas != nullptr) owner.canvas->repaint();
            }
        };
        terminalToggle.onClick = [this] {
            if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && node->type == "sm_state")
            {
                node->terminal = terminalToggle.getToggleState();
                if (owner.canvas != nullptr) owner.canvas->repaint();
            }
        };
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff151b22));
        auto area = getLocalBounds().reduced(8);

        drawLabel(g, "Diagram", diagramName.getBounds().translated(0, -18));

        if (const auto* node = owner.findNode(owner.selectedNodeUid))
            drawNode(g, area, *node);
        else if (owner.selectedConnectionIndex >= 0 && owner.selectedConnectionIndex < (int)owner.connections.size())
            drawWire(g, area, owner.connections[(size_t)owner.selectedConnectionIndex]);
        else
            drawHelp(g, area, "Select a node to edit its useful properties.");
    }

    void resized() override
    {
        syncEditorsToSelection();
        auto area = getLocalBounds().reduced(8);
        area.removeFromTop(18);
        diagramName.setBounds(area.removeFromTop(26));
        area.removeFromTop(18);

        integerValue.setVisible(false);
        stringValue.setVisible(false);
        hideStateMachineEditors();
        for (auto* editor : inputEditors)
            editor->setVisible(false);

        if (const auto* node = owner.findNode(owner.selectedNodeUid))
        {
            if (node->type.startsWith("sm_") || node->type == "state_machine_instance")
            {
                layoutStateMachineEditors(*node, area);
            }
            else if (node->type == "literal_i64")
            {
                area.removeFromTop(42);
                integerValue.setBounds(area.removeFromTop(26));
                integerValue.setVisible(true);
            }
            else if (node->type == "literal_string")
            {
                area.removeFromTop(42);
                stringValue.setBounds(area.removeFromTop(52));
                stringValue.setVisible(true);
            }

            layoutInputEditors(*node, area);
        }
    }

    void refreshFromModel()
    {
        syncEditorsToSelection();
        resized();
        repaint();
    }

private:
    void configureEditor(juce::TextEditor& editor)
    {
        editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff202a33));
        editor.setColour(juce::TextEditor::textColourId, juce::Colour(0xfff0f6f8));
        editor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff40525f));
        editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff7fffd4));
        editor.setFont(juce::Font(13.0f));
    }

    void syncEditorsToSelection()
    {
        if (diagramName.getText() != owner.diagramName)
            diagramName.setText(owner.diagramName, juce::dontSendNotification);

        if (const auto* node = owner.findNode(owner.selectedNodeUid))
        {
            if (node->type.startsWith("sm_") || node->type == "state_machine_instance")
            {
                syncingEditors = true;
                const auto displayName = node->textValue.isNotEmpty() ? node->textValue : node->id;
                if (nodeName.getText() != displayName)
                    nodeName.setText(displayName, juce::dontSendNotification);
                if (accessibility.getText() != node->accessibility)
                    accessibility.setText(node->accessibility, juce::dontSendNotification);
                if (entryAction.getText() != node->entryAction)
                    entryAction.setText(node->entryAction, juce::dontSendNotification);
                if (updateAction.getText() != node->updateAction)
                    updateAction.setText(node->updateAction, juce::dontSendNotification);
                if (exitAction.getText() != node->exitAction)
                    exitAction.setText(node->exitAction, juce::dontSendNotification);
                if (eventName.getText() != node->eventName)
                    eventName.setText(node->eventName, juce::dontSendNotification);
                if (payloadType.getText() != node->payloadType)
                    payloadType.setText(node->payloadType, juce::dontSendNotification);
                if (guardExpression.getText() != node->guardExpression)
                    guardExpression.setText(node->guardExpression, juce::dontSendNotification);
                if (transitionAction.getText() != node->transitionAction)
                    transitionAction.setText(node->transitionAction, juce::dontSendNotification);
                if (machineRef.getText() != node->machineRef)
                    machineRef.setText(node->machineRef, juce::dontSendNotification);
                initialToggle.setToggleState(node->initial, juce::dontSendNotification);
                terminalToggle.setToggleState(node->terminal, juce::dontSendNotification);
                syncingEditors = false;
            }

            if (node->type == "literal_i64" && integerValue.getText() != juce::String(node->literalValue))
                integerValue.setText(juce::String(node->literalValue), juce::dontSendNotification);
            if (node->type == "literal_string" && stringValue.getText() != node->textValue)
                stringValue.setText(node->textValue, juce::dontSendNotification);

            for (int i = 0; i < inputEditors.size(); ++i)
            {
                if (i >= (int)node->inputs.size())
                    continue;
                const auto value = i < (int)node->inputDefaults.size() && node->inputDefaults[(size_t)i].isNotEmpty()
                    ? node->inputDefaults[(size_t)i]
                    : defaultValueFor(node->inputs[(size_t)i]);
                if (inputEditors[i]->getText() != value)
                    inputEditors[i]->setText(value, juce::dontSendNotification);
            }
        }
    }

    void layoutInputEditors(const GraphNode& node, juce::Rectangle<int> area)
    {
        int y = area.getY();
        if (node.type == "event_start") y += 78;
        if (node.type == "print") y += 78;
        y += 26;

        for (int i = 0; i < (int)node.inputs.size() && i < inputEditors.size(); ++i)
        {
            const auto& pin = node.inputs[(size_t)i];
            if (pin.flow != PinFlow::Data || owner.connectionToInput(node.uid, i) != nullptr)
            {
                y += 30;
                continue;
            }

            auto field = juce::Rectangle<int>(area.getX() + 88, y, area.getWidth() - 88, 24);
            inputEditors[i]->setBounds(field);
            inputEditors[i]->setVisible(true);
            y += 30;
        }
    }

    void hideStateMachineEditors()
    {
        for (auto* editor : { &nodeName, &accessibility, &entryAction, &updateAction, &exitAction,
                              &eventName, &payloadType, &guardExpression, &transitionAction, &machineRef })
            editor->setVisible(false);
        initialToggle.setVisible(false);
        terminalToggle.setVisible(false);
    }

    void layoutStateMachineEditors(const GraphNode& node, juce::Rectangle<int> area)
    {
        area.removeFromTop(42);
        nodeName.setBounds(area.removeFromTop(26));
        nodeName.setVisible(true);
        area.removeFromTop(24);

        if (node.type == "sm_state")
        {
            accessibility.setBounds(area.removeFromTop(26));
            accessibility.setVisible(true);
            area.removeFromTop(8);
            initialToggle.setBounds(area.removeFromTop(24));
            terminalToggle.setBounds(area.removeFromTop(24));
            initialToggle.setVisible(true);
            terminalToggle.setVisible(true);
            area.removeFromTop(20);
            entryAction.setBounds(area.removeFromTop(28));
            updateAction.setBounds(area.removeFromTop(28));
            exitAction.setBounds(area.removeFromTop(28));
            entryAction.setVisible(true);
            updateAction.setVisible(true);
            exitAction.setVisible(true);
        }
        else if (node.type == "sm_transition")
        {
            eventName.setBounds(area.removeFromTop(26));
            eventName.setVisible(true);
            area.removeFromTop(24);
            guardExpression.setBounds(area.removeFromTop(28));
            transitionAction.setBounds(area.removeFromTop(28));
            guardExpression.setVisible(true);
            transitionAction.setVisible(true);
        }
        else if (node.type == "sm_event")
        {
            eventName.setBounds(area.removeFromTop(26));
            payloadType.setBounds(area.removeFromTop(26));
            eventName.setVisible(true);
            payloadType.setVisible(true);
        }
        else if (node.type == "state_machine_instance")
        {
            machineRef.setBounds(area.removeFromTop(26));
            machineRef.setVisible(true);
        }
    }

    void updateSelectedStateText(juce::TextEditor& source, juce::String GraphNode::*field)
    {
        if (syncingEditors) return;
        if (auto* node = owner.findNode(owner.selectedNodeUid); node != nullptr && node->type == "sm_state")
        {
            node->*field = source.getText().trim();
            if (owner.canvas != nullptr) owner.canvas->repaint();
        }
    }

    juce::String uniqueFallbackId(const GraphNode& node) const
    {
        return node.type == "sm_transition" ? "transition_" + juce::String(node.uid)
             : node.type == "sm_event" ? "event_" + juce::String(node.uid)
             : "state_" + juce::String(node.uid);
    }

    void drawLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> bounds)
    {
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff9aa8ba));
        g.drawText(text, bounds, juce::Justification::centredLeft, true);
    }

    void drawNode(juce::Graphics& g, juce::Rectangle<int>& area, const GraphNode& node)
    {
        auto y = 70;
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.setColour(node.colour);
        g.drawText(node.title, 8, y, getWidth() - 16, 22, juce::Justification::centredLeft, true);
        y += 30;

        if (node.type == "literal_i64")
        {
            drawLabel(g, "Value", integerValue.getBounds().translated(0, -18));
            y = integerValue.getBottom() + 14;
        }
        else if (node.type == "literal_string")
        {
            drawLabel(g, "Text", stringValue.getBounds().translated(0, -18));
            y = stringValue.getBottom() + 14;
        }
        else if (node.type == "event_start")
        {
            drawHelp(g, { 8, y, getWidth() - 16, 70 }, "Execution begins here. Wire its Start output to action nodes like Print.");
            y += 78;
        }
        else if (node.type == "print")
        {
            drawHelp(g, { 8, y, getWidth() - 16, 70 }, "Print writes its Value input to the IDE console/output target when execution reaches it.");
            y += 78;
        }
        else if (node.type == "sm_state")
        {
            drawLabel(g, "Name", nodeName.getBounds().translated(0, -18));
            drawLabel(g, "Access", accessibility.getBounds().translated(0, -18));
            drawLabel(g, "Actions", entryAction.getBounds().translated(0, -18));
            drawLabel(g, "Entry", entryAction.getBounds().translated(-52, 0));
            drawLabel(g, "Update", updateAction.getBounds().translated(-52, 0));
            drawLabel(g, "Exit", exitAction.getBounds().translated(-52, 0));
            y = exitAction.getBottom() + 14;
        }
        else if (node.type == "sm_transition")
        {
            drawLabel(g, "Name", nodeName.getBounds().translated(0, -18));
            drawLabel(g, "Event", eventName.getBounds().translated(0, -18));
            drawLabel(g, "Guard", guardExpression.getBounds().translated(0, -18));
            drawLabel(g, "Action", transitionAction.getBounds().translated(0, -18));
            y = transitionAction.getBottom() + 14;
        }
        else if (node.type == "sm_event")
        {
            drawLabel(g, "Name", nodeName.getBounds().translated(0, -18));
            drawLabel(g, "Event", eventName.getBounds().translated(0, -18));
            drawLabel(g, "Payload", payloadType.getBounds().translated(0, -18));
            y = payloadType.getBottom() + 14;
        }
        else if (node.type == "state_machine_instance")
        {
            drawLabel(g, "Name", nodeName.getBounds().translated(0, -18));
            drawLabel(g, "Machine", machineRef.getBounds().translated(0, -18));
            drawHelp(g, { 8, machineRef.getBottom() + 14, getWidth() - 16, 78 },
                     "This node hosts a state-machine schematic. Exec/lib graphs drive it with tick, event, and payload inputs.");
            y = machineRef.getBottom() + 104;
        }

        drawInputDefaults(g, { 8, y, getWidth() - 16, getHeight() - y - 8 }, node);
    }

    void drawInputDefaults(juce::Graphics& g, juce::Rectangle<int> area, const GraphNode& node)
    {
        if (node.inputs.empty()) return;

        drawLabel(g, "Inputs", area.removeFromTop(18));
        g.setFont(juce::Font(12.0f));

        for (int i = 0; i < (int)node.inputs.size(); ++i)
        {
            const auto& pin = node.inputs[(size_t)i];
            auto row = area.removeFromTop(30);
            g.setColour(pinColour(pin));
            g.fillRoundedRectangle(row.removeFromLeft(10).withSizeKeepingCentre(8, 8).toFloat(), 4.0f);
            row.removeFromLeft(5);

            if (const auto* wire = owner.connectionToInput(node.uid, i))
            {
                const auto* source = owner.findNode(wire->fromNode);
                g.setColour(juce::Colour(0xff758294));
                g.drawText(pin.name + " : " + pin.type + "  wired from " + (source != nullptr ? source->title : juce::String("unknown")),
                           row, juce::Justification::centredLeft, true);
            }
            else
            {
                g.setColour(juce::Colour(0xffdce9ee));
                if (pin.flow == PinFlow::Data && i < inputEditors.size())
                    g.drawText(pin.name + " : " + pin.type, row.removeFromLeft(82), juce::Justification::centredLeft, true);
                else
                    g.drawText(pin.name + " : " + pin.type, row, juce::Justification::centredLeft, true);
            }
        }
    }

    juce::String defaultValueFor(const Pin& pin) const
    {
        if (pin.type == "bool") return "false";
        if (pin.type == "string") return "\"\"";
        if (pin.type == "exec") return "flow";
        if (pin.flow == PinFlow::Stream) return "stream";
        if (pin.flow == PinFlow::Resource) return "resource";
        return "0";
    }

    void drawWire(juce::Graphics& g, juce::Rectangle<int>& area, const Connection& c)
    {
        const auto* from = owner.findNode(c.fromNode);
        const auto* to = owner.findNode(c.toNode);
        drawHelp(g, area, "Wire: " + (from ? from->title : "?") + " -> " + (to ? to->title : "?"));
    }

    void drawHelp(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
    {
        g.setColour(juce::Colour(0xff1b232c));
        g.fillRoundedRectangle(area.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xffdce9ee));
        g.setFont(juce::Font(12.0f));
        g.drawFittedText(text, area.reduced(8), juce::Justification::centredLeft, 4);
    }

    NodeDesignerPanel& owner;
    static constexpr int maxInputEditors = 12;
    juce::TextEditor diagramName;
    juce::TextEditor integerValue;
    juce::TextEditor stringValue;
    juce::TextEditor nodeName;
    juce::TextEditor accessibility;
    juce::TextEditor entryAction;
    juce::TextEditor updateAction;
    juce::TextEditor exitAction;
    juce::TextEditor eventName;
    juce::TextEditor payloadType;
    juce::TextEditor guardExpression;
    juce::TextEditor transitionAction;
    juce::TextEditor machineRef;
    juce::ToggleButton initialToggle;
    juce::ToggleButton terminalToggle;
    bool syncingEditors = false;
    juce::OwnedArray<juce::TextEditor> inputEditors;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InspectorPanel)
};

class NodeDesignerPanel::VariablesPanel final : public juce::Component
{
public:
    explicit VariablesPanel(NodeDesignerPanel& ownerIn) : owner(ownerIn) {}

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff15181d));
        auto area = getLocalBounds().reduced(6);

        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.setColour(juce::Colours::white);
        g.drawText("Variables", area.removeFromTop(24), juce::Justification::centredLeft, true);

        auto header = area.removeFromTop(22);
        g.setColour(juce::Colour(0xff20262f));
        g.fillRect(header);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff9aa8ba));
        g.drawText("Name", header.removeFromLeft(76).reduced(5, 0), juce::Justification::centredLeft, true);
        g.drawText("Type", header.removeFromLeft(48).reduced(5, 0), juce::Justification::centredLeft, true);
        g.drawText("Access", header.reduced(5, 0), juce::Justification::centredLeft, true);

        g.setFont(juce::Font(12.0f));
        int y = area.getY() + 4;
        for (const auto& v : owner.variables)
        {
            auto row = juce::Rectangle<int>(area.getX(), y, area.getWidth(), 24);
            g.setColour(juce::Colour(0xff1b232c));
            g.fillRoundedRectangle(row.toFloat(), 4.0f);
            auto cols = row.reduced(5, 0);
            g.setColour(juce::Colour(0xffdce9ee));
            g.drawText(v.name, cols.removeFromLeft(76), juce::Justification::centredLeft, true);
            g.setColour(juce::Colour(0xff7fffd4));
            g.drawText(v.type, cols.removeFromLeft(48), juce::Justification::centredLeft, true);
            g.setColour(v.access == "read_write" ? juce::Colour(0xfff5c15a)
                                                   : (v.access == "read_only" ? juce::Colour(0xff80bfff) : juce::Colour(0xff9aa8ba)));
            g.drawText(accessLabel(v.access), cols, juce::Justification::centredLeft, true);
            y += 28;
        }

        if (owner.variables.empty())
        {
            g.setColour(juce::Colour(0xff758294));
            g.drawText("No schematic variables yet.", area.reduced(4, 6), juce::Justification::centredLeft, true);
        }
    }

private:
    static juce::String accessLabel(const juce::String& access)
    {
        if (access == "read_only") return "read only";
        if (access == "read_write") return "read/write";
        return "local";
    }

    NodeDesignerPanel& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VariablesPanel)
};

class NodeDesignerPanel::TypesPanel final : public juce::Component
{
public:
    explicit TypesPanel(NodeDesignerPanel& ownerIn) : owner(ownerIn) {}

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff15181d));
        auto area = getLocalBounds().reduced(6);

        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.setColour(juce::Colours::white);
        g.drawText("Types", area.removeFromTop(24), juce::Justification::centredLeft, true);

        drawPrimitiveLegend(g, area.removeFromTop(48));
        area.removeFromTop(5);

        for (const auto& type : owner.types)
            drawType(g, area, type);

        if (owner.types.empty())
        {
            g.setColour(juce::Colour(0xff758294));
            g.setFont(juce::Font(12.0f));
            g.drawText("No user types yet.", area.reduced(4, 6), juce::Justification::centredLeft, true);
        }
    }

private:
    void drawPrimitiveLegend(juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour(juce::Colour(0xff1b232c));
        g.fillRoundedRectangle(area.toFloat(), 4.0f);

        g.setFont(juce::Font(11.0f));
        const juce::StringArray primitives { "i64", "f64", "bool", "string", "ascii" };
        auto x = area.getX() + 6;
        auto y = area.getY() + 7;
        for (const auto& primitive : primitives)
        {
            auto pill = juce::Rectangle<int>(x, y, 38, 16);
            g.setColour(typeColour(primitive));
            g.fillRoundedRectangle(pill.toFloat(), 8.0f);
            g.setColour(juce::Colours::white);
            g.drawText(primitive, pill, juce::Justification::centred, true);
            x += 42;
            if (x + 38 > area.getRight())
            {
                x = area.getX() + 6;
                y += 19;
            }
        }
    }

    void drawType(juce::Graphics& g, juce::Rectangle<int>& area, const TypeDef& type)
    {
        if (area.getHeight() < 42) return;

        int rows = juce::jmax((int)type.fields.size(), (int)type.variants.size());
        const int h = juce::jlimit(50, 116, 36 + rows * 17);
        auto box = area.removeFromTop(juce::jmin(h, area.getHeight()));
        area.removeFromTop(6);

        g.setColour(juce::Colour(0xff1b232c));
        g.fillRoundedRectangle(box.toFloat(), 5.0f);
        g.setColour(type.kind == "struct" ? juce::Colour(0xff4b8fc8) : juce::Colour(0xff9c5a9c));
        g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 5.0f, 1.0f);

        auto inner = box.reduced(8, 5);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xffdce9ee));
        g.drawText(type.name, inner.removeFromTop(16), juce::Justification::centredLeft, true);

        g.setFont(juce::Font(10.5f));
        g.setColour(juce::Colour(0xff9aa8ba));
        g.drawText(type.kind + "  fallback: " + type.targetFallback, inner.removeFromTop(15), juce::Justification::centredLeft, true);

        g.setFont(juce::Font(11.0f));
        if (type.kind == "struct")
        {
            for (const auto& f : type.fields)
                drawMember(g, inner.removeFromTop(17), f.name, f.type);
        }
        else
        {
            for (const auto& v : type.variants)
            {
                juce::String shape = juce::String(v.intValue);
                if (!v.fields.empty())
                    shape << " / payload";
                drawMember(g, inner.removeFromTop(17), v.name, shape);
            }
        }
    }

    void drawMember(juce::Graphics& g, juce::Rectangle<int> row, const juce::String& name, const juce::String& type)
    {
        g.setColour(juce::Colour(0xffdce9ee));
        g.drawText(name, row.removeFromLeft(88), juce::Justification::centredLeft, true);
        g.setColour(typeColour(type));
        g.drawText(type, row, juce::Justification::centredLeft, true);
    }

    static juce::Colour typeColour(const juce::String& type)
    {
        if (type.startsWith("i") || type == "int") return juce::Colour(0xff80bfff);
        if (type.startsWith("f") || type == "float") return juce::Colour(0xff5ad8a0);
        if (type == "bool") return juce::Colour(0xffd85a5a);
        if (type == "string" || type == "ascii") return juce::Colour(0xffd85ad0);
        return juce::Colour(0xffc7d2df);
    }

    NodeDesignerPanel& owner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TypesPanel)
};

std::vector<NodeDesignerPanel::NodeTemplate> NodeDesignerPanel::buildTemplates()
{
    auto in = [](juce::String name, juce::String type = "i64") { return Pin { name, type, PinFlow::Data, true }; };
    auto out = [](juce::String name, juce::String type = "i64") { return Pin { name, type, PinFlow::Data, false }; };
    auto execIn = [](juce::String name = "in") { return Pin { name, "exec", PinFlow::Exec, true }; };
    auto execOut = [](juce::String name = "then") { return Pin { name, "exec", PinFlow::Exec, false }; };
    auto streamIn = [](juce::String name, juce::String type) { return Pin { name, type, PinFlow::Stream, true }; };
    auto streamOut = [](juce::String name, juce::String type) { return Pin { name, type, PinFlow::Stream, false }; };
    auto resourceIn = [](juce::String name, juce::String type) { return Pin { name, type, PinFlow::Resource, true }; };
    auto resourceOut = [](juce::String name, juce::String type) { return Pin { name, type, PinFlow::Resource, false }; };
    return {
        { "param_i64", "Parameter", "Sources", juce::Colour(0xff2f9c9c), 0, {}, { out("x") } },
        { "literal_i64", "Integer", "Sources", juce::Colour(0xff2f6fb8), 10, {}, { out("value") } },
        { "literal_string", "String", "Sources", juce::Colour(0xffd85ad0), 0, {}, { out("value", "string") } },
        { "const_bool", "Boolean", "Sources", juce::Colour(0xff2f6fb8), 0, {}, { out("value", "bool") } },
        { "add", "Add", "Math", juce::Colour(0xff5f9c5a), 0, { in("a"), in("b") }, { out("sum") } },
        { "sub", "Subtract", "Math", juce::Colour(0xff5f9c5a), 0, { in("a"), in("b") }, { out("difference") } },
        { "mul", "Multiply", "Math", juce::Colour(0xff5f9c5a), 0, { in("a"), in("b") }, { out("product") } },
        { "div", "Divide", "Math", juce::Colour(0xff5f9c5a), 0, { in("a"), in("b") }, { out("quotient") } },
        { "mod", "Modulo", "Math", juce::Colour(0xff5f9c5a), 0, { in("a"), in("b") }, { out("remainder") } },
        { "eq", "Equals", "Logic", juce::Colour(0xffb8722f), 0, { in("a"), in("b") }, { out("is equal", "bool") } },
        { "lt", "Less Than", "Logic", juce::Colour(0xffb8722f), 0, { in("a"), in("b") }, { out("is less", "bool") } },
        { "gt", "Greater Than", "Logic", juce::Colour(0xffb8722f), 0, { in("a"), in("b") }, { out("is greater", "bool") } },
        { "if", "If Select", "Logic", juce::Colour(0xff9c5a9c), 0, { in("cond", "bool"), in("then"), in("else") }, { out("value") } },
        { "event_start", "Event Start", "Execution", juce::Colour(0xffb8722f), 0, {}, { execOut("start") } },
        { "print", "Print", "Execution", juce::Colour(0xffb8722f), 0, { execIn(), in("value", "any") }, { execOut("then") } },
        { "sequence", "Sequence", "Execution", juce::Colour(0xffb8722f), 0, { execIn() }, { execOut("A"), execOut("B") } },
        { "branch", "Branch", "Execution", juce::Colour(0xffb8722f), 0, { execIn(), in("condition", "bool") }, { execOut("true"), execOut("false") } },
        { "while_loop", "While Loop", "Execution", juce::Colour(0xffb8722f), 0, { execIn(), in("condition", "bool") }, { execOut("body"), execOut("done") } },
        { "for_loop", "For Loop", "Execution", juce::Colour(0xffb8722f), 0, { execIn(), in("first"), in("last") }, { execOut("body"), execOut("done"), out("index") } },
        { "end", "End", "Execution", juce::Colour(0xffb8722f), 0, { execIn() }, {} },
        { "return", "Return", "Execution", juce::Colour(0xffb8722f), 0, { execIn(), in("value") }, {} },
        { "get_var", "Get Variable", "Variables", juce::Colour(0xff2f9c9c), 0, {}, { out("value") } },
        { "set_var", "Set Variable", "Variables", juce::Colour(0xff2f9c9c), 0, { execIn(), in("value") }, { execOut("then") } },
        { "make_struct", "Make Struct", "Types", juce::Colour(0xff9c5a9c), 0, { in("field A", "any"), in("field B", "any") }, { out("struct", "any") } },
        { "break_struct", "Break Struct", "Types", juce::Colour(0xff9c5a9c), 0, { in("struct", "any") }, { out("field A", "any"), out("field B", "any") } },
        { "enum_value", "Enum Value", "Types", juce::Colour(0xff9c5a9c), 0, {}, { out("variant", "any") } },
        { "match_enum", "Match Enum", "Execution", juce::Colour(0xffb8722f), 0, { execIn(), in("variant", "any") }, { execOut("case A"), execOut("case B"), execOut("default") } },
        { "audio_buffer_in", "Audio Buffer In", "Streams", juce::Colour(0xff9c8a5a), 0, {}, { streamOut("buffer", "AudioBuffer") } },
        { "process_audio_block", "Process Audio Block", "Streams", juce::Colour(0xff9c8a5a), 0, { streamIn("in", "AudioBuffer") }, { streamOut("out", "AudioBuffer") } },
        { "audio_buffer_out", "Audio Buffer Out", "Streams", juce::Colour(0xff9c8a5a), 0, { streamIn("buffer", "AudioBuffer") }, {} },
        { "texture_resource", "Texture Resource", "Resources", juce::Colour(0xff9c5a9c), 0, {}, { resourceOut("texture", "Texture") } },
        { "sample_texture", "Sample Texture", "Resources", juce::Colour(0xff9c5a9c), 0, { resourceIn("texture", "Texture"), in("uv", "any") }, { out("color", "any") } }
        ,
        { "reroute", "Reroute", "Graph", juce::Colour(0xff7fffd4), 0, { in("in", "any") }, { out("out", "any") } },
        { "state_machine_instance", "State Machine", "State Machine", juce::Colour(0xff4b8fc8), 0,
          { execIn("tick"), in("event", "string"), in("payload", "any") },
          { execOut("then"), out("state", "string"), out("transition", "string") } },
        { "sm_state", "State", "State Machine", juce::Colour(0xff4b8fc8), 0, { execIn("enter") }, { execOut("leave") } },
        { "sm_transition", "Transition", "State Machine", juce::Colour(0xff6cb6ff), 0, { execIn("from") }, { execOut("to") } },
        { "sm_event", "Event", "State Machine", juce::Colour(0xffb8722f), 0, {}, { out("event", "string") } }
    };
}

NodeDesignerPanel::NodeDesignerPanel()
    : templates(buildTemplates())
{
    palette = std::make_unique<PalettePanel>(*this);
    canvas = std::make_unique<GraphCanvas>(*this);
    inspector = std::make_unique<InspectorPanel>(*this);
    variablesPanel = std::make_unique<VariablesPanel>(*this);
    typesPanel = std::make_unique<TypesPanel>(*this);

    titleLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff7fffd4));
    addAndMakeVisible(titleLabel);

    for (auto* button : { &saveButton, &compileButton, &saveSourceButton })
    {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263942));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff0f6f8));
        addAndMakeVisible(*button);
    }
    saveButton.onClick = [this] { saveGraph(); };
    compileButton.onClick = [this] { compileGraph(); };
    saveSourceButton.onClick = [this] { saveGeneratedSource(); };

    diagramTypeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
    diagramTypeSelector.addItem("Node Graph", 1);
    diagramTypeSelector.addItem("State Machine", 2);
    diagramTypeSelector.setSelectedId(1, juce::dontSendNotification);
    diagramTypeSelector.onChange = [this] {
        const auto nextType = selectedDiagramType();
        if (nextType == diagramType) return;
        diagramType = nextType;
        if (diagramType == "state_machine")
            initializeStateMachine();
        else
            initializeNodeGraph();
        refreshProperties();
        resized();
        repaint();
    };
    addAndMakeVisible(diagramTypeLabel);
    addAndMakeVisible(diagramTypeSelector);

    targetLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
    targetSelector.addItem("Frust", 1);
    targetSelector.addItem("GLSL (production reference)", 2);
    targetSelector.setSelectedId(1, juce::dontSendNotification);
    targetSelector.onChange = [this] {
        const bool frust = selectedTarget() == "frust";
        frustProjectTypeLabel.setVisible(frust);
        frustProjectTypeSelector.setVisible(frust);
        if (selectedTarget() != "frust")
            setStatus("GLSL exists in production as reference. This research canvas is currently wired to the Frust backend.", true);
        refreshProperties();
        resized();
    };
    addAndMakeVisible(targetLabel);
    addAndMakeVisible(targetSelector);

    frustProjectTypeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce9ee));
    frustProjectTypeSelector.addItem("Executable pod (bin)", 1);
    frustProjectTypeSelector.addItem("Library pod (lib)", 2);
    frustProjectTypeSelector.setSelectedId(1, juce::dontSendNotification);
    frustProjectTypeSelector.onChange = [this] {
        frustProjectType = selectedFrustProjectType();
        setStatus("Frust output will be packaged as a Frate " + frustProjectType + " pod.");
    };
    addAndMakeVisible(frustProjectTypeLabel);
    addAndMakeVisible(frustProjectTypeSelector);

    propertiesTitle.setFont(juce::Font(15.0f, juce::Font::bold));
    propertiesTitle.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(propertiesTitle);
    addAndMakeVisible(*inspector);

    statusView.setMultiLine(false);
    statusView.setReadOnly(true);
    statusView.setScrollbarsShown(false);
    statusView.setFont(juce::Font(13.0f));
    statusView.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff182229));
    addAndMakeVisible(statusView);

    addAndMakeVisible(*palette);
    addAndMakeVisible(*variablesPanel);
    addAndMakeVisible(*typesPanel);
    addAndMakeVisible(*canvas);
    initializeUntitled();
}

NodeDesignerPanel::NodeDesignerPanel(const juce::File& fileToOpen)
    : NodeDesignerPanel()
{
    if (fileToOpen.existsAsFile())
        loadGraphFile(fileToOpen);
}

void NodeDesignerPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e2227));
}

void NodeDesignerPanel::resized()
{
    auto b = getLocalBounds().reduced(8);
    auto header = b.removeFromTop(30);
    titleLabel.setBounds(header.removeFromLeft(230));
    for (auto* button : { &saveButton, &compileButton, &saveSourceButton })
    {
        header.removeFromLeft(5);
        button->setBounds(header.removeFromLeft(button == &saveSourceButton ? 82 : 72));
    }
    header.removeFromLeft(14);
    diagramTypeLabel.setBounds(header.removeFromLeft(58));
    diagramTypeSelector.setBounds(header.removeFromLeft(150));
    header.removeFromLeft(8);
    targetLabel.setBounds(header.removeFromLeft(48));
    targetSelector.setBounds(header.removeFromLeft(210));
    header.removeFromLeft(8);
    const bool showFrustKind = selectedTarget() == "frust";
    frustProjectTypeLabel.setVisible(showFrustKind);
    frustProjectTypeSelector.setVisible(showFrustKind);
    if (showFrustKind)
    {
        frustProjectTypeLabel.setBounds(header.removeFromLeft(45));
        frustProjectTypeSelector.setBounds(header.removeFromLeft(170));
    }
    b.removeFromTop(7);

    auto status = b.removeFromBottom(32);
    b.removeFromBottom(8);

    auto left = b.removeFromLeft(220);
    b.removeFromLeft(8);
    auto right = b.removeFromRight(270);
    b.removeFromRight(8);

    auto typesArea = left.removeFromBottom(230);
    left.removeFromBottom(8);
    auto variablesArea = left.removeFromBottom(160);
    left.removeFromBottom(8);
    palette->setBounds(left);
    variablesPanel->setBounds(variablesArea);
    typesPanel->setBounds(typesArea);
    propertiesTitle.setBounds(right.removeFromTop(25).reduced(6, 0));
    inspector->setBounds(right);
    canvas->setBounds(b);
    statusView.setBounds(status);
}

NodeDesignerPanel::GraphNode* NodeDesignerPanel::findNode(int uid)
{
    auto it = std::find_if(nodes.begin(), nodes.end(), [uid](const GraphNode& n) { return n.uid == uid; });
    return it == nodes.end() ? nullptr : &*it;
}

const NodeDesignerPanel::GraphNode* NodeDesignerPanel::findNode(int uid) const
{
    auto it = std::find_if(nodes.begin(), nodes.end(), [uid](const GraphNode& n) { return n.uid == uid; });
    return it == nodes.end() ? nullptr : &*it;
}

const NodeDesignerPanel::NodeTemplate* NodeDesignerPanel::findTemplate(const juce::String& type) const
{
    auto it = std::find_if(templates.begin(), templates.end(), [&type](const NodeTemplate& t) { return t.type == type; });
    return it == templates.end() ? nullptr : &*it;
}

const NodeDesignerPanel::FunctionDef* NodeDesignerPanel::findFunction(const juce::String& id) const
{
    auto it = std::find_if(functions.begin(), functions.end(), [&id](const FunctionDef& f) {
        return f.id == id || f.name == id;
    });
    return it == functions.end() ? nullptr : &*it;
}

void NodeDesignerPanel::refreshTemplatesWithFunctions()
{
    templates = buildTemplates();

    for (const auto& fn : functions)
    {
        if (fn.id.isEmpty() || fn.name.isEmpty()) continue;
        if (fn.accessibility == "private") continue;

        NodeTemplate t;
        t.type = "call_function:" + fn.id;
        t.title = fn.namespaceName.isNotEmpty() ? fn.namespaceName + "." + fn.name : fn.name;
        t.category = "Functions";
        t.colour = juce::Colour(0xff4b8fc8);
        t.defaultLiteral = 0;
        t.functionRef = fn.id;

        if (fn.kind == "callable")
            t.inputs.push_back({ "in", "exec", PinFlow::Exec, true });
        for (const auto& input : fn.inputs)
            t.inputs.push_back({ input.name, input.type, PinFlow::Data, true });

        if (fn.kind == "callable")
            t.outputs.push_back({ "then", "exec", PinFlow::Exec, false });
        for (const auto& output : fn.outputs)
            t.outputs.push_back({ output.name, output.type, PinFlow::Data, false });

        templates.push_back(std::move(t));
    }

    if (palette != nullptr)
        palette->refreshRows();
}

int NodeDesignerPanel::nextUid()
{
    return nextNodeUid++;
}

juce::String NodeDesignerPanel::uniqueNodeId(const juce::String& base) const
{
    auto clean = base.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_").toLowerCase();
    if (clean.isEmpty()) clean = "node";
    auto candidate = clean;
    int suffix = 2;
    auto exists = [this](const juce::String& id) {
        return std::any_of(nodes.begin(), nodes.end(), [&id](const GraphNode& n) { return n.id == id; });
    };
    while (exists(candidate))
        candidate = clean + "_" + juce::String(suffix++);
    return candidate;
}

NodeDesignerPanel::GraphNode& NodeDesignerPanel::addNode(const juce::String& type, juce::Point<float> world)
{
    const auto* t = findTemplate(type);
    if (t == nullptr) t = findTemplate("literal_i64");

    GraphNode node;
    node.uid = nextUid();
    node.id = uniqueNodeId(baseIdForType(t->type));
    node.type = t->type;
    node.title = t->title;
    node.category = t->category;
    node.colour = t->colour;
    node.functionRef = t->functionRef;
    node.x = world.x;
    node.y = world.y;
    node.literalValue = t->defaultLiteral;
    node.inputs = t->inputs;
    node.outputs = t->outputs;
    if (node.type == "sm_state")
    {
        node.textValue = "State";
        node.accessibility = "package";
    }
    else if (node.type == "sm_transition")
    {
        node.textValue = "Transition";
        node.eventName = "event";
    }
    else if (node.type == "sm_event")
    {
        node.textValue = "Event";
        node.eventName = "event";
    }
    else if (node.type == "state_machine_instance")
    {
        node.textValue = "State Machine";
        node.machineRef = "";
    }
    for (const auto& input : node.inputs)
    {
        if (input.type == "bool") node.inputDefaults.push_back("false");
        else if (input.type == "string") node.inputDefaults.push_back("\"\"");
        else if (input.type == "any") node.inputDefaults.push_back("");
        else node.inputDefaults.push_back("0");
    }
    nodes.push_back(node);
    selectedNodeUid = node.uid;
    selectedConnectionIndex = -1;
    refreshProperties();
    setStatus("Added " + node.title + " node.");
    return nodes.back();
}

NodeDesignerPanel::GraphNode& NodeDesignerPanel::addRerouteNode(const Connection& connection, juce::Point<float> world)
{
    auto& node = addNode("reroute", world);
    const auto* source = findNode(connection.fromNode);
    if (source != nullptr && connection.fromPin >= 0 && connection.fromPin < (int)source->outputs.size())
    {
        const auto pin = source->outputs[(size_t)connection.fromPin];
        if (!node.inputs.empty())
        {
            node.inputs[0].type = pin.type;
            node.inputs[0].flow = pin.flow;
        }
        if (!node.outputs.empty())
        {
            node.outputs[0].type = pin.type;
            node.outputs[0].flow = pin.flow;
        }
    }
    node.title = "Reroute";
    node.textValue = "Reroute";
    setStatus("Inserted reroute node.");
    return node;
}

void NodeDesignerPanel::removeNode(int uid)
{
    connections.erase(std::remove_if(connections.begin(), connections.end(), [uid](const Connection& c) {
        return c.fromNode == uid || c.toNode == uid;
    }), connections.end());
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [uid](const GraphNode& n) { return n.uid == uid; }), nodes.end());
}

const NodeDesignerPanel::Connection* NodeDesignerPanel::connectionToInput(int nodeUid, int inputPin) const
{
    auto it = std::find_if(connections.begin(), connections.end(), [nodeUid, inputPin](const Connection& c) {
        return c.toNode == nodeUid && c.toPin == inputPin;
    });
    return it == connections.end() ? nullptr : &*it;
}

const NodeDesignerPanel::GraphNode* NodeDesignerPanel::resolveRerouteUpstream(int nodeUid, int inputPin) const
{
    const auto* wire = connectionToInput(nodeUid, inputPin);
    auto* node = wire != nullptr ? findNode(wire->fromNode) : nullptr;
    std::set<int> visited;
    while (node != nullptr && node->type == "reroute" && visited.count(node->uid) == 0)
    {
        visited.insert(node->uid);
        wire = connectionToInput(node->uid, 0);
        node = wire != nullptr ? findNode(wire->fromNode) : nullptr;
    }
    return node;
}

const NodeDesignerPanel::GraphNode* NodeDesignerPanel::resolveRerouteDownstream(int nodeUid, int outputPin) const
{
    auto it = std::find_if(connections.begin(), connections.end(), [nodeUid, outputPin](const Connection& c) {
        return c.fromNode == nodeUid && c.fromPin == outputPin;
    });
    auto* node = it != connections.end() ? findNode(it->toNode) : nullptr;
    std::set<int> visited;
    while (node != nullptr && node->type == "reroute" && visited.count(node->uid) == 0)
    {
        visited.insert(node->uid);
        it = std::find_if(connections.begin(), connections.end(), [node](const Connection& c) {
            return c.fromNode == node->uid && c.fromPin == 0;
        });
        node = it != connections.end() ? findNode(it->toNode) : nullptr;
    }
    return node;
}

bool NodeDesignerPanel::pinsCompatible(const Pin& from, const Pin& to) const
{
    if (from.flow != to.flow) return false;
    return from.type == to.type || from.type == "any" || to.type == "any";
}

bool NodeDesignerPanel::isCompileableFrustDataNode(const GraphNode& node) const
{
    static const juce::StringArray supported {
        "param_i64", "literal_i64", "add", "sub", "mul", "div", "mod", "eq", "lt", "gt", "if"
    };
    return supported.contains(node.type);
}

bool NodeDesignerPanel::hasExecutableGraph() const
{
    const bool hasStart = std::any_of(nodes.begin(), nodes.end(), [](const GraphNode& n) { return n.type == "event_start"; });
    const bool hasTerminal = std::any_of(nodes.begin(), nodes.end(), [](const GraphNode& n) { return n.type == "end" || n.type == "return"; });
    return hasStart && hasTerminal;
}

juce::String NodeDesignerPanel::validateFrustCompileable() const
{
    if (hasExecutableGraph())
        return {};

    for (const auto& node : nodes)
    {
        if (node.type == "reroute")
            continue;
        if (!isCompileableFrustDataNode(node))
            return "The Frust backend currently lowers pure data-flow expression nodes only. '" + node.title
                + "' is authored in the graph model but not wired to Frust codegen yet.";

        for (const auto& pin : node.inputs)
            if (pin.flow != PinFlow::Data)
                return "The Frust backend currently lowers data pins only. '" + node.title
                    + "' has a " + flowName(pin.flow) + " input pin.";
        for (const auto& pin : node.outputs)
            if (pin.flow != PinFlow::Data)
                return "The Frust backend currently lowers data pins only. '" + node.title
                    + "' has a " + flowName(pin.flow) + " output pin.";
    }

    for (const auto& node : nodes)
    {
        if (node.type == "param_i64" || node.type == "literal_i64")
            continue;
        for (int i = 0; i < (int)node.inputs.size(); ++i)
        {
            const bool hasInput = std::any_of(connections.begin(), connections.end(), [&node, i](const Connection& c) {
                return c.toNode == node.uid && c.toPin == i;
            });
            if (!hasInput)
                return "Missing input '" + node.inputs[(size_t)i].name + "' on node '" + node.title + "'.";
        }
    }

    if (outputNodeId().isEmpty())
        return "The graph needs an output node before it can compile.";

    return {};
}

juce::String NodeDesignerPanel::validateStateMachine() const
{
    int stateCount = 0;
    int initialCount = 0;
    int transitionCount = 0;
    std::set<juce::String> stateIds;

    for (const auto& node : nodes)
    {
        if (node.type != "sm_state")
            continue;

        ++stateCount;
        if (node.id.trim().isEmpty())
            return "A state has no id.";
        if (stateIds.count(node.id) != 0)
            return "State id '" + node.id + "' is duplicated.";
        stateIds.insert(node.id);
        if (node.initial)
            ++initialCount;
    }

    if (stateCount == 0)
        return "A state machine needs at least one State node.";
    if (initialCount == 0)
        return "A state machine needs one initial State.";
    if (initialCount > 1)
        return "A state machine can only have one initial State.";

    for (const auto& node : nodes)
    {
        if (node.type != "sm_transition")
            continue;

        ++transitionCount;
        if (node.eventName.trim().isEmpty())
            return "Transition '" + node.id + "' needs an event name.";

        const auto* from = resolveRerouteUpstream(node.uid, 0);
        if (from == nullptr || from->type != "sm_state")
            return "Transition '" + node.id + "' needs a source State wired to its input.";

        const auto* to = resolveRerouteDownstream(node.uid, 0);
        if (to == nullptr || to->type != "sm_state")
            return "Transition '" + node.id + "' needs a destination State wired from its output.";
    }

    if (transitionCount == 0 && stateCount > 1)
        return "A multi-state machine needs at least one Transition.";

    return {};
}

juce::String NodeDesignerPanel::expressionForInput(const GraphNode& node, int inputIndex) const
{
    if (const auto* wire = connectionToInput(node.uid, inputIndex))
    {
        if (const auto* source = findNode(wire->fromNode))
        {
            std::set<int> visited;
            while (source != nullptr && source->type == "reroute" && visited.count(source->uid) == 0)
            {
                visited.insert(source->uid);
                const auto* upstream = connectionToInput(source->uid, 0);
                source = upstream != nullptr ? findNode(upstream->fromNode) : nullptr;
            }
            if (source == nullptr)
                return "0";
            if (source->type == "literal_i64") return juce::String(source->literalValue);
            if (source->type == "literal_string") return quoted(source->textValue);
            if (source->type == "const_bool") return source->literalValue != 0 ? "true" : "false";
            if (source->type == "param_i64") return source->id;
            return source->id;
        }
    }

    if (inputIndex >= 0 && inputIndex < (int)node.inputDefaults.size())
    {
        const auto value = node.inputDefaults[(size_t)inputIndex];
        if (value.isNotEmpty()) return value;
    }
    return "0";
}

juce::String NodeDesignerPanel::buildExecutableFrustSource() const
{
    juce::String source;
    if (selectedFrustProjectType() == "lib")
        source << "pub fn run_nodes() -> i64 = {\n";
    else
        source << "fn main() -> i64 = {\n";

    for (const auto& node : nodes)
    {
        if (node.type == "print")
            source << "    println(" << expressionForInput(node, 1) << ");\n";
    }

    source << "    0\n";
    source << "}\n";
    return source;
}

void NodeDesignerPanel::connectPins(int fromNode, int fromPin, int toNode, int toPin)
{
    auto* a = findNode(fromNode);
    auto* b = findNode(toNode);
    if (a == nullptr || b == nullptr) return;
    if (fromPin < 0 || fromPin >= (int)a->outputs.size() || toPin < 0 || toPin >= (int)b->inputs.size()) return;
    if (!pinsCompatible(a->outputs[(size_t)fromPin], b->inputs[(size_t)toPin]))
    {
        setStatus("Those pins are not compatible.", true);
        return;
    }

    if (a->type == "sm_state" && b->type == "sm_state" && a->outputs[(size_t)fromPin].flow == PinFlow::Exec)
    {
        const auto fromStateUid = a->uid;
        const auto toStateUid = b->uid;
        const auto fromStateId = a->id;
        const auto toStateId = b->id;
        const auto fromStateName = a->textValue.isNotEmpty() ? a->textValue : a->id;
        const auto toStateName = b->textValue.isNotEmpty() ? b->textValue : b->id;
        const auto transitionId = uniqueNodeId(fromStateId + "_to_" + toStateId);
        const auto world = juce::Point<float> { (a->x + b->x) * 0.5f, (a->y + b->y) * 0.5f };

        auto& transition = addNode("sm_transition", world);
        transition.id = transitionId;
        transition.textValue = fromStateName + " to " + toStateName;
        transition.eventName = transitionId;
        const auto transitionUid = transition.uid;

        connections.push_back({ fromStateUid, fromPin, transitionUid, 0 });
        connections.push_back({ transitionUid, 0, toStateUid, toPin });
        selectedNodeUid = transitionUid;
        refreshProperties();
        setStatus("Created transition " + transition.textValue + ". Edit its event and guard in Properties.");
        return;
    }

    const bool allowMultipleInput = b->type == "sm_state" && b->inputs[(size_t)toPin].flow == PinFlow::Exec;
    if (!allowMultipleInput)
    {
        connections.erase(std::remove_if(connections.begin(), connections.end(), [toNode, toPin](const Connection& c) {
            return c.toNode == toNode && c.toPin == toPin;
        }), connections.end());
    }
    connections.push_back({ fromNode, fromPin, toNode, toPin });
    refreshProperties();
    setStatus("Connected " + a->title + " to " + b->title + ".");
}

void NodeDesignerPanel::initializeUntitled()
{
    initializeNodeGraph();
}

void NodeDesignerPanel::initializeNodeGraph()
{
    currentGraphFile = {};
    currentSourceFile = {};
    generatedSource.clear();
    generatedFiles.clear();
    diagramType = "node_graph";
    diagramTypeSelector.setSelectedId(1, juce::dontSendNotification);
    diagramName = "Untitled Node Schematic";
    titleLabel.setText(diagramName, juce::dontSendNotification);
    nodes.clear();
    connections.clear();
    variables.clear();
    types.clear();
    functions.clear();
    resources.clear();
    states.clear();
    events.clear();
    transitions.clear();
    nextNodeUid = 1;

    variables.push_back({ "scratch", "i64", "local", "0" });
    variables.push_back({ "inputGain", "f64", "read_only", "1.0" });
    variables.push_back({ "state", "i64", "read_write", "0" });
    types.push_back({ "Player",
                      "struct",
                      "struct",
                      { { "name", "string" }, { "health", "i64" }, { "isAwake", "bool" } },
                      {} });
    types.push_back({ "Command",
                      "algebraic_enum",
                      "tagged_int",
                      {},
                      { { "Move", { { "direction", "string" } }, 1 },
                        { "Look", {}, 2 },
                        { "Say", { { "text", "string" } }, 3 } } });
    refreshTemplatesWithFunctions();

    diagramName = "Hello Nodes";
    titleLabel.setText(diagramName, juce::dontSendNotification);

    auto& start = addNode("event_start", { 40.0f, 155.0f });
    auto& text = addNode("literal_string", { 40.0f, 260.0f });
    text.textValue = "Hello from nodes";
    auto& print = addNode("print", { 300.0f, 175.0f });
    auto& end = addNode("end", { 560.0f, 175.0f });

    connectPins(start.uid, 0, print.uid, 0);
    connectPins(text.uid, 0, print.uid, 1);
    connectPins(print.uid, 0, end.uid, 0);
    selectedNodeUid = print.uid;
    refreshProperties();
    setStatus("Hello Nodes: execution starts at Start, Print writes a value, End terminates the diagram.");
}

void NodeDesignerPanel::initializeStateMachine()
{
    currentGraphFile = {};
    currentSourceFile = {};
    generatedSource.clear();
    generatedFiles.clear();
    diagramType = "state_machine";
    diagramTypeSelector.setSelectedId(2, juce::dontSendNotification);
    diagramName = "Assistant State Machine";
    titleLabel.setText(diagramName, juce::dontSendNotification);

    nodes.clear();
    connections.clear();
    variables.clear();
    types.clear();
    functions.clear();
    resources.clear();
    states.clear();
    events.clear();
    transitions.clear();
    selectedNodeUid = 0;
    selectedConnectionIndex = -1;
    nextNodeUid = 1;

    variables.push_back({ "retryCount", "i64", "local", "0" });
    events.push_back({ "push_to_talk", "PushToTalkPressed", "" });
    events.push_back({ "speech_done", "UserFinishedSpeaking", "" });
    events.push_back({ "response_ready", "EngineerResponseReady", "" });
    events.push_back({ "cancel", "Cancel", "" });

    refreshTemplatesWithFunctions();

    auto& idle = addNode("sm_state", { 70.0f, 180.0f });
    idle.id = "idle";
    idle.textValue = "Idle";
    idle.entryAction = "restore_audio_routing";
    idle.initial = true;
    const auto idleUid = idle.uid;

    auto& listen = addNode("sm_state", { 390.0f, 95.0f });
    listen.id = "listening";
    listen.textValue = "Listening";
    listen.entryAction = "route_microphone_to_engineer";
    const auto listenUid = listen.uid;

    auto& think = addNode("sm_state", { 720.0f, 180.0f });
    think.id = "thinking";
    think.textValue = "Thinking";
    think.entryAction = "send_transcript_to_engineer";
    const auto thinkUid = think.uid;

    auto& speak = addNode("sm_state", { 390.0f, 330.0f });
    speak.id = "speaking";
    speak.textValue = "Speaking";
    speak.entryAction = "speak_response_to_headset";
    const auto speakUid = speak.uid;

    auto& t1 = addNode("sm_transition", { 285.0f, 180.0f });
    t1.id = "idle_to_listening";
    t1.textValue = "Idle to Listening";
    t1.eventName = "push_to_talk";
    const auto t1Uid = t1.uid;
    connectPins(idleUid, 0, t1Uid, 0);
    connectPins(t1Uid, 0, listenUid, 0);

    auto& t2 = addNode("sm_transition", { 600.0f, 95.0f });
    t2.id = "listening_to_thinking";
    t2.textValue = "Listening to Thinking";
    t2.eventName = "speech_done";
    const auto t2Uid = t2.uid;
    connectPins(listenUid, 0, t2Uid, 0);
    connectPins(t2Uid, 0, thinkUid, 0);

    auto& t3 = addNode("sm_transition", { 600.0f, 330.0f });
    t3.id = "thinking_to_speaking";
    t3.textValue = "Thinking to Speaking";
    t3.eventName = "response_ready";
    const auto t3Uid = t3.uid;
    connectPins(thinkUid, 0, t3Uid, 0);
    connectPins(t3Uid, 0, speakUid, 0);

    auto& t4 = addNode("sm_transition", { 180.0f, 330.0f });
    t4.id = "speaking_to_idle";
    t4.textValue = "Speaking to Idle";
    t4.eventName = "cancel";
    const auto t4Uid = t4.uid;
    connectPins(speakUid, 0, t4Uid, 0);
    connectPins(t4Uid, 0, idleUid, 0);

    selectedNodeUid = idleUid;
    refreshProperties();
    if (canvas != nullptr) canvas->repaint();
    setStatus("State Machine: drag State and Transition nodes, wire exec pins to define the machine, and edit behavior in Properties.");
}

void NodeDesignerPanel::loadGraphFile(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        setStatus("Node schematic does not exist: " + file.getFullPathName(), true);
        return;
    }
    currentGraphFile = file;
    currentSourceFile = file.withFileExtension(".fr");
    titleLabel.setText(file.getFileName(), juce::dontSendNotification);
    loadFromJson(file.loadFileAsString());
    setStatus("Opened " + file.getFullPathName());
}

void NodeDesignerPanel::loadFromJson(const juce::String& text)
{
    const auto root = juce::JSON::parse(text);
    if (!root.isObject())
    {
        setStatus("Invalid schematic JSON.", true);
        return;
    }

    nodes.clear();
    connections.clear();
    variables.clear();
    types.clear();
    functions.clear();
    resources.clear();
    states.clear();
    events.clear();
    transitions.clear();
    nextNodeUid = 1;
    diagramName = propertyText(root, "name", "Untitled Node Schematic");
    titleLabel.setText(diagramName, juce::dontSendNotification);
    diagramType = propertyText(root, "diagramType", propertyText(root, "kind", "node_graph"));
    if (diagramType == "stateMachine") diagramType = "state_machine";
    diagramTypeSelector.setSelectedId(diagramType == "state_machine" ? 2 : 1, juce::dontSendNotification);
    frustProjectType = "bin";
    auto targetOptionsVar = root.getProperty("targetOptions", {});
    auto frustOptionsVar = targetOptionsVar.getProperty("frust", {});
    frustProjectType = propertyText(frustOptionsVar, "projectType", frustProjectType);
    frustProjectTypeSelector.setSelectedId(frustProjectType == "lib" ? 2 : 1, juce::dontSendNotification);
    std::map<juce::String, int> ids;

    auto resourcesVar = root.getProperty("resources", {});
    if (auto* arr = resourcesVar.getArray())
        for (const auto& r : *arr)
            resources.push_back({
                propertyText(r, "id", propertyText(r, "name", "resource")),
                propertyText(r, "name", "resource"),
                propertyText(r, "kind", "resource"),
                propertyText(r, "accessibility", "private")
            });

    auto eventsVar = root.getProperty("events", {});
    if (auto* arr = eventsVar.getArray())
        for (const auto& e : *arr)
            events.push_back({
                propertyText(e, "id", propertyText(e, "name", "event")),
                propertyText(e, "name", "event"),
                propertyText(e, "payloadType", propertyText(e, "message"))
            });

    auto statesVar = root.getProperty("states", {});
    if (auto* arr = statesVar.getArray())
        for (const auto& s : *arr)
            states.push_back({
                propertyText(s, "id", propertyText(s, "name", "state")),
                propertyText(s, "name", "State"),
                propertyText(s, "accessibility", "package"),
                propertyText(s, "entryAction", propertyText(s, "entry")),
                propertyText(s, "updateAction", propertyText(s, "update")),
                propertyText(s, "exitAction", propertyText(s, "exit")),
                propertyFloat(s, "x", 120.0f),
                propertyFloat(s, "y", 120.0f),
                (bool)s.getProperty("initial", false),
                (bool)s.getProperty("terminal", false)
            });

    auto transitionsVar = root.getProperty("transitions", {});
    if (auto* arr = transitionsVar.getArray())
        for (const auto& t : *arr)
            transitions.push_back({
                propertyText(t, "id", propertyText(t, "from") + "_to_" + propertyText(t, "to")),
                propertyText(t, "from"),
                propertyText(t, "to"),
                propertyText(t, "event"),
                propertyText(t, "guard"),
                propertyText(t, "action")
            });

    auto subgraphsVar = root.getProperty("subgraphs", {});
    if (auto* arr = subgraphsVar.getArray())
    {
        for (const auto& f : *arr)
        {
            FunctionDef def;
            def.id = propertyText(f, "id", propertyText(f, "name", "function"));
            def.name = propertyText(f, "name", propertyText(f, "title", def.id));
            def.namespaceName = propertyText(f, "namespace", propertyText(root, "namespace"));
            def.accessibility = propertyText(f, "accessibility", "package");
            def.kind = propertyText(f, "kind", "pure");
            auto ownerVar = f.getProperty("owner", {});
            def.owningPod = propertyText(ownerVar, "pod");
            def.owningVersion = propertyText(ownerVar, "version");

            auto inputsVar = f.getProperty("inputs", f.getProperty("params", {}));
            if (auto* inputs = inputsVar.getArray())
                for (const auto& p : *inputs)
                    def.inputs.push_back({ propertyText(p, "name", "value"), propertyText(p, "type", "i64") });

            auto outputsVar = f.getProperty("outputs", {});
            if (auto* outputs = outputsVar.getArray())
                for (const auto& p : *outputs)
                    def.outputs.push_back({ propertyText(p, "name", "result"), propertyText(p, "type", "i64") });
            else if (auto outputName = propertyText(f, "output"); outputName.isNotEmpty())
                def.outputs.push_back({ outputName, propertyText(f, "returnType", "i64") });

            functions.push_back(std::move(def));
        }
    }
    refreshTemplatesWithFunctions();

    auto paramsVar = root.getProperty("params", {});
    if (auto* params = paramsVar.getArray())
    {
        float y = 90.0f;
        for (const auto& p : *params)
        {
            auto& node = addNode("param_i64", { propertyFloat(p, "x", 20.0f), propertyFloat(p, "y", y) });
            node.id = propertyText(p, "name", "x");
            if (!node.outputs.empty()) node.outputs[0].name = node.id;
            ids[node.id] = node.uid;
            y += 88.0f;
        }
    }

    auto nodesVar = root.getProperty("nodes", {});
    if (auto* arr = nodesVar.getArray())
    {
        for (const auto& n : *arr)
        {
            const auto type = propertyText(n, "type", "literal_i64");
            juce::String templateType = type;
            if (type == "call_function")
            {
                auto fnVar = n.getProperty("function", {});
                const auto functionId = propertyText(fnVar, "id", propertyText(fnVar, "name", propertyText(n, "functionRef")));
                if (findFunction(functionId) == nullptr && functionId.isNotEmpty())
                {
                    FunctionDef external;
                    external.id = functionId;
                    external.name = propertyText(fnVar, "name", functionId);
                    external.namespaceName = propertyText(fnVar, "namespace");
                    external.accessibility = "public";
                    external.kind = propertyText(fnVar, "kind", "pure");
                    external.owningPod = propertyText(fnVar, "pod");
                    external.owningVersion = propertyText(fnVar, "version");

                    auto signatureInputs = fnVar.getProperty("inputs", {});
                    if (auto* inputs = signatureInputs.getArray())
                        for (const auto& p : *inputs)
                            external.inputs.push_back({ propertyText(p, "name", "value"), propertyText(p, "type", "any") });
                    else
                    {
                        auto callInputsVar = n.getProperty("inputs", {});
                        if (auto* callInputs = callInputsVar.getArray())
                            for (int i = 0; i < callInputs->size(); ++i)
                                external.inputs.push_back({ "arg" + juce::String(i + 1), "any" });
                    }

                    auto signatureOutputs = fnVar.getProperty("outputs", {});
                    if (auto* outputs = signatureOutputs.getArray())
                        for (const auto& p : *outputs)
                            external.outputs.push_back({ propertyText(p, "name", "result"), propertyText(p, "type", "any") });
                    else
                        external.outputs.push_back({ "result", propertyText(fnVar, "returnType", "any") });

                    functions.push_back(std::move(external));
                    refreshTemplatesWithFunctions();
                }
                templateType = "call_function:" + functionId;
            }
            auto& node = addNode(templateType, { propertyFloat(n, "x", 220.0f), propertyFloat(n, "y", 120.0f) });
            node.id = propertyText(n, "id", uniqueNodeId(baseIdForType(type)));
            node.type = type == "call_function" ? templateType : node.type;
            node.literalValue = propertyInt(n, "value", node.literalValue);
            node.textValue = propertyText(n, "text", node.textValue);
            node.accessibility = propertyText(n, "accessibility", node.accessibility);
            node.entryAction = propertyText(n, "entryAction", propertyText(n, "entry", node.entryAction));
            node.updateAction = propertyText(n, "updateAction", propertyText(n, "update", node.updateAction));
            node.exitAction = propertyText(n, "exitAction", propertyText(n, "exit", node.exitAction));
            node.eventName = propertyText(n, "event", node.eventName);
            node.payloadType = propertyText(n, "payloadType", node.payloadType);
            node.guardExpression = propertyText(n, "guard", node.guardExpression);
            node.transitionAction = propertyText(n, "action", node.transitionAction);
            node.machineRef = propertyText(n, "machineRef", node.machineRef);
            node.initial = (bool)n.getProperty("initial", node.initial);
            node.terminal = (bool)n.getProperty("terminal", node.terminal);
            if (node.type == "reroute")
            {
                const auto pinType = propertyText(n, "pinType", "any");
                const auto pinFlow = flowFromName(propertyText(n, "pinFlow", "data"));
                if (!node.inputs.empty())
                {
                    node.inputs[0].type = pinType;
                    node.inputs[0].flow = pinFlow;
                }
                if (!node.outputs.empty())
                {
                    node.outputs[0].type = pinType;
                    node.outputs[0].flow = pinFlow;
                }
            }
            node.functionRef = propertyText(n.getProperty("function", {}), "id", propertyText(n, "functionRef", node.functionRef));
            auto inputsVar = n.getProperty("inputs", {});
            if (auto* inputs = inputsVar.getArray())
            {
                for (int i = 0; i < inputs->size() && i < (int)node.inputDefaults.size(); ++i)
                {
                    const auto& input = inputs->getReference(i);
                    const auto value = propertyText(input, "default");
                    if (value.isNotEmpty())
                        node.inputDefaults[(size_t)i] = value;
                }
            }
            ids[node.id] = node.uid;
        }

        for (const auto& n : *arr)
        {
            const auto toId = propertyText(n, "id");
            const auto toIt = ids.find(toId);
            if (toIt == ids.end()) continue;
            auto inputsVar = n.getProperty("inputs", {});
            auto* inputs = inputsVar.getArray();
            if (inputs == nullptr) continue;
            for (int i = 0; i < inputs->size(); ++i)
            {
                const auto& input = inputs->getReference(i);
                auto sourceId = propertyText(input, "ref");
                if (sourceId.isEmpty()) sourceId = propertyText(input, "param");
                if (sourceId.isEmpty()) continue;
                auto fromIt = ids.find(sourceId);
                if (fromIt == ids.end()) continue;
                connectPins(fromIt->second, 0, toIt->second, i);
            }
        }
    }

    if (diagramType == "state_machine" && nodes.empty() && !states.empty())
    {
        std::map<juce::String, int> stateNodeUids;
        for (const auto& s : states)
        {
            auto& node = addNode("sm_state", { s.x, s.y });
            node.id = s.id;
            node.textValue = s.name;
            node.accessibility = s.accessibility;
            node.entryAction = s.entryAction;
            node.updateAction = s.updateAction;
            node.exitAction = s.exitAction;
            node.initial = s.initial;
            node.terminal = s.terminal;
            stateNodeUids[s.id] = node.uid;
        }

        for (const auto& t : transitions)
        {
            const auto fromIt = stateNodeUids.find(t.from);
            const auto toIt = stateNodeUids.find(t.to);
            if (fromIt == stateNodeUids.end() || toIt == stateNodeUids.end())
                continue;

            const auto* fromState = findNode(fromIt->second);
            const auto* toState = findNode(toIt->second);
            const auto x = fromState != nullptr && toState != nullptr ? (fromState->x + toState->x) * 0.5f : 260.0f;
            const auto y = fromState != nullptr && toState != nullptr ? (fromState->y + toState->y) * 0.5f : 160.0f;
            auto& node = addNode("sm_transition", { x, y });
            node.id = t.id;
            node.textValue = t.id;
            node.eventName = t.event;
            node.guardExpression = t.guard;
            node.transitionAction = t.action;
            const auto transitionUid = node.uid;
            connectPins(fromIt->second, 0, transitionUid, 0);
            connectPins(transitionUid, 0, toIt->second, 0);
        }
    }

    auto variablesVar = root.getProperty("variables", {});
    if (auto* arr = variablesVar.getArray())
    {
        for (const auto& v : *arr)
            variables.push_back({
                propertyText(v, "name", "value"),
                propertyText(v, "type", "i64"),
                propertyText(v, "access", "local"),
                propertyText(v, "default", "0")
            });
    }

    auto typesVar = root.getProperty("types", {});
    if (auto* arr = typesVar.getArray())
    {
        for (const auto& t : *arr)
        {
            TypeDef def;
            def.name = propertyText(t, "name", "Type");
            def.kind = propertyText(t, "kind", "struct");
            def.targetFallback = propertyText(t, "targetFallback", def.kind == "struct" ? "struct" : "tagged_int");

            auto fieldsVar = t.getProperty("fields", {});
            if (auto* fields = fieldsVar.getArray())
                for (const auto& f : *fields)
                    def.fields.push_back({ propertyText(f, "name", "field"), propertyText(f, "type", "i64") });

            auto variantsVar = t.getProperty("variants", {});
            if (auto* variants = variantsVar.getArray())
                for (const auto& v : *variants)
                {
                    TypeVariant variant;
                    variant.name = propertyText(v, "name", "Case");
                    variant.intValue = propertyInt(v, "intValue", (int)def.variants.size() + 1);
                    auto payloadVar = v.getProperty("fields", {});
                    if (auto* payload = payloadVar.getArray())
                        for (const auto& f : *payload)
                            variant.fields.push_back({ propertyText(f, "name", "value"), propertyText(f, "type", "i64") });
                    def.variants.push_back(variant);
                }

            types.push_back(def);
        }
    }

    auto debugVar = root.getProperty("debug", {});
    auto breakpointsVar = debugVar.getProperty("breakpoints", {});
    if (auto* breakpoints = breakpointsVar.getArray())
        for (const auto& bp : *breakpoints)
            if (auto* n = findNode(ids[propertyText(bp.getProperty("target", {}), "nodeId")]))
                n->breakpoint = (bool)bp.getProperty("enabled", true);

    auto watchesVar = debugVar.getProperty("watches", {});
    if (auto* watches = watchesVar.getArray())
        for (const auto& watch : *watches)
            if (auto* n = findNode(ids[propertyText(watch.getProperty("target", {}), "nodeId")]))
                n->watched = (bool)watch.getProperty("enabled", true);

    selectedNodeUid = nodes.empty() ? 0 : nodes.back().uid;
    refreshProperties();
    if (canvas != nullptr) canvas->repaint();
    if (variablesPanel != nullptr) variablesPanel->repaint();
    if (typesPanel != nullptr) typesPanel->repaint();
}

juce::String NodeDesignerPanel::quoted(const juce::String& text)
{
    return juce::JSON::toString(juce::var(text));
}

juce::String NodeDesignerPanel::outputNodeId() const
{
    if (const auto* selected = findNode(selectedNodeUid); selected != nullptr && selected->type != "param_i64" && selected->type != "reroute")
        return selected->id;
    for (const auto& n : nodes)
        if (n.id == "result" && n.type != "param_i64" && n.type != "reroute")
            return n.id;
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
        if (it->type != "param_i64" && it->type != "reroute")
            return it->id;
    return {};
}

juce::String NodeDesignerPanel::buildCompilerJson(bool includeRoutingNodes) const
{
    juce::String text;
    text << "{\n";
    text << "  \"functionName\": \"compute\",\n";
    text << "  \"params\": [\n";
    bool firstParam = true;
    for (const auto& n : nodes)
    {
        if (n.type != "param_i64") continue;
        if (!firstParam) text << ",\n";
        firstParam = false;
        text << "    { \"name\": " << quoted(n.id) << ", \"type\": \"i64\", \"x\": " << n.x << ", \"y\": " << n.y << " }";
    }
    text << "\n  ],\n";
    text << "  \"output\": " << quoted(outputNodeId()) << ",\n";
    text << "  \"nodes\": [\n";

    bool firstNode = true;
    for (const auto& n : nodes)
    {
        if (n.type == "param_i64") continue;
        if (!includeRoutingNodes && n.type == "reroute") continue;
        if (!firstNode) text << ",\n";
        firstNode = false;
        const bool functionCall = n.type.startsWith("call_function:");
        text << "    { \"id\": " << quoted(n.id) << ", \"type\": " << quoted(functionCall ? "call_function" : n.type)
             << ", \"x\": " << n.x << ", \"y\": " << n.y;
        if (functionCall)
        {
            const auto* fn = findFunction(n.functionRef);
            text << ", \"function\": { \"id\": " << quoted(n.functionRef);
            if (fn != nullptr)
            {
                text << ", \"namespace\": " << quoted(fn->namespaceName)
                     << ", \"name\": " << quoted(fn->name);
                if (fn->owningPod.isNotEmpty())
                    text << ", \"pod\": " << quoted(fn->owningPod);
                if (fn->owningVersion.isNotEmpty())
                    text << ", \"version\": " << quoted(fn->owningVersion);
            }
            text << " }";
        }
        if (n.type == "literal_i64")
            text << ", \"value\": " << n.literalValue;
        if (n.type == "literal_string" || n.type.startsWith("sm_") || n.type == "state_machine_instance")
            text << ", \"text\": " << quoted(n.textValue);
        if (n.type == "sm_state")
        {
            text << ", \"accessibility\": " << quoted(n.accessibility)
                 << ", \"initial\": " << (n.initial ? "true" : "false")
                 << ", \"terminal\": " << (n.terminal ? "true" : "false");
            if (n.entryAction.isNotEmpty()) text << ", \"entryAction\": " << quoted(n.entryAction);
            if (n.updateAction.isNotEmpty()) text << ", \"updateAction\": " << quoted(n.updateAction);
            if (n.exitAction.isNotEmpty()) text << ", \"exitAction\": " << quoted(n.exitAction);
        }
        else if (n.type == "sm_transition")
        {
            text << ", \"event\": " << quoted(n.eventName);
            if (n.guardExpression.isNotEmpty()) text << ", \"guard\": " << quoted(n.guardExpression);
            if (n.transitionAction.isNotEmpty()) text << ", \"action\": " << quoted(n.transitionAction);
        }
        else if (n.type == "sm_event")
        {
            text << ", \"event\": " << quoted(n.eventName);
            if (n.payloadType.isNotEmpty()) text << ", \"payloadType\": " << quoted(n.payloadType);
        }
        else if (n.type == "reroute")
        {
            const auto pinType = !n.outputs.empty() ? n.outputs[0].type : juce::String("any");
            const auto pinFlow = !n.outputs.empty() ? n.outputs[0].flow : PinFlow::Data;
            text << ", \"pinType\": " << quoted(pinType)
                 << ", \"pinFlow\": " << quoted(flowName(pinFlow));
        }
        else if (n.type == "state_machine_instance")
        {
            text << ", \"machineRef\": " << quoted(n.machineRef);
        }

        if (!n.inputs.empty())
        {
            text << ", \"inputs\": [ ";
            for (int i = 0; i < (int)n.inputs.size(); ++i)
            {
                if (i != 0) text << ", ";
                auto connIt = std::find_if(connections.begin(), connections.end(), [&n, i](const Connection& c) {
                    return c.toNode == n.uid && c.toPin == i;
                });
                if (connIt == connections.end())
                {
                    const auto value = i < (int)n.inputDefaults.size() ? n.inputDefaults[(size_t)i] : juce::String();
                    text << "{ \"default\": " << quoted(value) << " }";
                }
                else
                {
                    auto* source = findNode(connIt->fromNode);
                    std::set<int> visited;
                    while (!includeRoutingNodes && source != nullptr && source->type == "reroute" && visited.count(source->uid) == 0)
                    {
                        visited.insert(source->uid);
                        const auto* upstream = connectionToInput(source->uid, 0);
                        source = upstream != nullptr ? findNode(upstream->fromNode) : nullptr;
                    }
                    if (source != nullptr && source->type == "param_i64")
                        text << "{ \"param\": " << quoted(source->id) << " }";
                    else if (source != nullptr)
                        text << "{ \"ref\": " << quoted(source->id) << " }";
                    else
                        text << "{ \"ref\": \"__missing_input__\" }";
                }
            }
            text << " ]";
        }
        text << " }";
    }
    text << "\n  ]\n}";
    return text;
}

juce::String NodeDesignerPanel::buildSchematicJson(bool includeRoutingNodes) const
{
    auto text = buildCompilerJson(includeRoutingNodes);
    auto insert = juce::String("  \"schemaVersion\": 2,\n")
        + "  \"name\": " + quoted(diagramName) + ",\n"
        + "  \"diagramType\": " + quoted(diagramType) + ",\n"
        + "  \"targets\": [ \"frust\" ],\n"
        + "  \"targetOptions\": { \"frust\": { \"functionName\": \"compute\", \"projectType\": " + quoted(selectedFrustProjectType()) + " }, \"glsl\": { \"referenceOnly\": true } },\n"
        + "  \"accessibility\": \"graph\",\n"
        + "  \"agentAccess\": { \"visible\": true, \"editable\": true, \"requiresApproval\": false },\n";
    const auto prefixLength = juce::String("{\n").length();
    if (text.startsWith("{\n"))
        text = "{\n" + insert + text.substring(prefixLength);

    juce::String vars;
    vars << ",\n  \"variables\": [";
    bool firstVar = true;
    for (const auto& v : variables)
    {
        if (!firstVar) vars << ", ";
        firstVar = false;
        vars << "{ \"name\": " << quoted(v.name)
             << ", \"type\": " << quoted(v.type)
             << ", \"access\": " << quoted(v.access)
             << ", \"default\": " << quoted(v.defaultValue) << " }";
    }
    vars << "]";

    juce::String typeJson;
    typeJson << ",\n  \"types\": [";
    bool firstType = true;
    for (const auto& t : types)
    {
        if (!firstType) typeJson << ", ";
        firstType = false;
        typeJson << "{ \"name\": " << quoted(t.name)
                 << ", \"kind\": " << quoted(t.kind)
                 << ", \"targetFallback\": " << quoted(t.targetFallback);

        if (!t.fields.empty())
        {
            typeJson << ", \"fields\": [";
            bool firstField = true;
            for (const auto& f : t.fields)
            {
                if (!firstField) typeJson << ", ";
                firstField = false;
                typeJson << "{ \"name\": " << quoted(f.name) << ", \"type\": " << quoted(f.type) << " }";
            }
            typeJson << "]";
        }

        if (!t.variants.empty())
        {
            typeJson << ", \"variants\": [";
            bool firstVariant = true;
            for (const auto& v : t.variants)
            {
                if (!firstVariant) typeJson << ", ";
                firstVariant = false;
                typeJson << "{ \"name\": " << quoted(v.name) << ", \"intValue\": " << v.intValue;
                if (!v.fields.empty())
                {
                    typeJson << ", \"fields\": [";
                    bool firstField = true;
                    for (const auto& f : v.fields)
                    {
                        if (!firstField) typeJson << ", ";
                        firstField = false;
                        typeJson << "{ \"name\": " << quoted(f.name) << ", \"type\": " << quoted(f.type) << " }";
                    }
                    typeJson << "]";
                }
                typeJson << " }";
            }
            typeJson << "]";
        }

        typeJson << " }";
    }
    typeJson << "]";

    juce::String functionJson;
    functionJson << ",\n  \"subgraphs\": [";
    bool firstFunction = true;
    for (const auto& f : functions)
    {
        if (!firstFunction) functionJson << ", ";
        firstFunction = false;
        functionJson << "{ \"id\": " << quoted(f.id)
                     << ", \"name\": " << quoted(f.name)
                     << ", \"namespace\": " << quoted(f.namespaceName)
                     << ", \"kind\": " << quoted(f.kind)
                     << ", \"accessibility\": " << quoted(f.accessibility);
        if (f.owningPod.isNotEmpty() || f.owningVersion.isNotEmpty())
            functionJson << ", \"owner\": { \"pod\": " << quoted(f.owningPod)
                         << ", \"version\": " << quoted(f.owningVersion) << " }";

        functionJson << ", \"inputs\": [";
        bool firstPort = true;
        for (const auto& p : f.inputs)
        {
            if (!firstPort) functionJson << ", ";
            firstPort = false;
            functionJson << "{ \"name\": " << quoted(p.name) << ", \"type\": " << quoted(p.type) << " }";
        }
        functionJson << "], \"outputs\": [";
        firstPort = true;
        for (const auto& p : f.outputs)
        {
            if (!firstPort) functionJson << ", ";
            firstPort = false;
            functionJson << "{ \"name\": " << quoted(p.name) << ", \"type\": " << quoted(p.type) << " }";
        }
        functionJson << "], \"nodes\": [] }";
    }
    functionJson << "]";

    juce::String resourceJson;
    resourceJson << ",\n  \"resources\": [";
    bool firstResource = true;
    for (const auto& r : resources)
    {
        if (!firstResource) resourceJson << ", ";
        firstResource = false;
        resourceJson << "{ \"id\": " << quoted(r.id)
                     << ", \"name\": " << quoted(r.name)
                     << ", \"kind\": " << quoted(r.kind)
                     << ", \"accessibility\": " << quoted(r.accessibility) << " }";
    }
    resourceJson << "]";

    juce::String eventJson;
    eventJson << ",\n  \"events\": [";
    bool firstEvent = true;
    std::set<juce::String> exportedEvents;
    auto writeEvent = [&](const juce::String& id, const juce::String& name, const juce::String& payload) {
        auto cleanId = id.isNotEmpty() ? id : name.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_").toLowerCase();
        if (cleanId.isEmpty() || exportedEvents.count(cleanId) != 0) return;
        exportedEvents.insert(cleanId);
        {
            if (!firstEvent) eventJson << ", ";
            firstEvent = false;
            eventJson << "{ \"id\": " << quoted(cleanId)
                      << ", \"name\": " << quoted(name.isNotEmpty() ? name : cleanId);
            if (payload.isNotEmpty())
                eventJson << ", \"payloadType\": " << quoted(payload);
            eventJson << " }";
        }
    };
    for (const auto& e : events)
        writeEvent(e.id, e.name, e.payloadType);
    for (const auto& n : nodes)
    {
        if (n.type == "sm_event")
            writeEvent(n.eventName.isNotEmpty() ? n.eventName : n.id, n.textValue.isNotEmpty() ? n.textValue : n.eventName, n.payloadType);
        else if (n.type == "sm_transition")
            writeEvent(n.eventName, n.eventName, {});
    }
    eventJson << "]";

    juce::String stateJson;
    stateJson << ",\n  \"states\": [";
    bool firstState = true;
    for (const auto& n : nodes)
    {
        if (n.type != "sm_state") continue;
        if (!firstState) stateJson << ", ";
        firstState = false;
        stateJson << "{ \"id\": " << quoted(n.id)
                  << ", \"name\": " << quoted(n.textValue.isNotEmpty() ? n.textValue : n.id)
                  << ", \"accessibility\": " << quoted(n.accessibility)
                  << ", \"x\": " << n.x
                  << ", \"y\": " << n.y
                  << ", \"initial\": " << (n.initial ? "true" : "false")
                  << ", \"terminal\": " << (n.terminal ? "true" : "false");
        if (n.entryAction.isNotEmpty()) stateJson << ", \"entryAction\": " << quoted(n.entryAction);
        if (n.updateAction.isNotEmpty()) stateJson << ", \"updateAction\": " << quoted(n.updateAction);
        if (n.exitAction.isNotEmpty()) stateJson << ", \"exitAction\": " << quoted(n.exitAction);
        stateJson << " }";
    }
    stateJson << "]";

    juce::String transitionJson;
    transitionJson << ",\n  \"transitions\": [";
    bool firstTransition = true;
    for (const auto& n : nodes)
    {
        if (n.type != "sm_transition") continue;
        const auto* from = [&]() -> const GraphNode* {
            auto it = std::find_if(connections.begin(), connections.end(), [&n](const Connection& c) {
                return c.toNode == n.uid && c.toPin == 0;
            });
            auto* node = it == connections.end() ? nullptr : findNode(it->fromNode);
            std::set<int> visited;
            while (node != nullptr && node->type == "reroute" && visited.count(node->uid) == 0)
            {
                visited.insert(node->uid);
                const auto* upstream = connectionToInput(node->uid, 0);
                node = upstream != nullptr ? findNode(upstream->fromNode) : nullptr;
            }
            return node;
        }();
        const auto* to = [&]() -> const GraphNode* {
            auto it = std::find_if(connections.begin(), connections.end(), [&n](const Connection& c) {
                return c.fromNode == n.uid && c.fromPin == 0;
            });
            auto* node = it == connections.end() ? nullptr : findNode(it->toNode);
            std::set<int> visited;
            while (node != nullptr && node->type == "reroute" && visited.count(node->uid) == 0)
            {
                visited.insert(node->uid);
                auto next = std::find_if(connections.begin(), connections.end(), [node](const Connection& c) {
                    return c.fromNode == node->uid && c.fromPin == 0;
                });
                node = next != connections.end() ? findNode(next->toNode) : nullptr;
            }
            return node;
        }();
        if (from == nullptr || to == nullptr || from->type != "sm_state" || to->type != "sm_state")
            continue;
        if (!firstTransition) transitionJson << ", ";
        firstTransition = false;
        transitionJson << "{ \"id\": " << quoted(n.id)
                       << ", \"from\": " << quoted(from->id)
                       << ", \"to\": " << quoted(to->id)
                       << ", \"event\": " << quoted(n.eventName);
        if (n.guardExpression.isNotEmpty()) transitionJson << ", \"guard\": " << quoted(n.guardExpression);
        if (n.transitionAction.isNotEmpty()) transitionJson << ", \"action\": " << quoted(n.transitionAction);
        transitionJson << " }";
    }
    transitionJson << "]";

    juce::String exports;
    exports << ",\n  \"exports\": { \"functions\": [";
    bool firstExport = true;
    for (const auto& f : functions)
    {
        if (f.accessibility != "public" && f.accessibility != "project" && f.accessibility != "package")
            continue;
        if (!firstExport) exports << ", ";
        firstExport = false;
        exports << quoted((f.namespaceName.isNotEmpty() ? f.namespaceName + "." : juce::String()) + f.name);
    }
    exports << "], \"types\": [";
    firstExport = true;
    for (const auto& t : types)
    {
        if (!firstExport) exports << ", ";
        firstExport = false;
        exports << quoted(t.name);
    }
    exports << "], \"resources\": [";
    firstExport = true;
    for (const auto& r : resources)
    {
        if (r.accessibility != "public" && r.accessibility != "project" && r.accessibility != "package")
            continue;
        if (!firstExport) exports << ", ";
        firstExport = false;
        exports << quoted(r.name);
    }
    exports << "] }";

    juce::String debug;
    debug << ",\n  \"debug\": {\n";
    debug << "    \"breakpoints\": [";
    bool first = true;
    for (const auto& n : nodes)
    {
        if (!n.breakpoint) continue;
        if (!first) debug << ", ";
        first = false;
        debug << "{ \"id\": " << quoted("bp_" + n.id) << ", \"target\": { \"kind\": \"node\", \"nodeId\": "
              << quoted(n.id) << " }, \"enabled\": true, \"condition\": \"\", \"hitCount\": 0 }";
    }
    debug << "],\n    \"watches\": [";
    first = true;
    for (const auto& n : nodes)
    {
        if (!n.watched) continue;
        if (!first) debug << ", ";
        first = false;
        debug << "{ \"id\": " << quoted("watch_" + n.id) << ", \"target\": { \"kind\": \"pin\", \"nodeId\": "
              << quoted(n.id) << ", \"pin\": \"output\" }, \"enabled\": true, \"label\": "
              << quoted(n.title + " output") << " }";
    }
    debug << "]\n  }";
    text = text.upToLastOccurrenceOf("\n}", false, false) + vars + typeJson + functionJson + resourceJson
        + eventJson + stateJson + transitionJson + exports + debug + "\n}";

    auto root = juce::JSON::parse(text);
    return root.isObject() ? juce::JSON::toString(root, true) : text;
}

juce::String NodeDesignerPanel::generatedSourcePathLabel() const
{
    const auto base = currentGraphFile.existsAsFile()
        ? currentGraphFile.getFileNameWithoutExtension()
        : diagramName.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-").toLowerCase();
    const auto safe = base.isNotEmpty() ? base : juce::String("node_schematic");
    return ".frust/generated/nodes/" + safe + "/" + (selectedFrustProjectType() == "lib" ? "lib.fr" : "main.fr");
}

juce::File NodeDesignerPanel::generatedSourceCacheFile() const
{
    juce::File root;
    if (currentGraphFile.existsAsFile())
        root = currentGraphFile.getParentDirectory();
    else
        root = juce::File::getCurrentWorkingDirectory();

    const auto label = generatedSourcePathLabel();
    return root.getChildFile(label);
}

juce::String NodeDesignerPanel::selectedDiagramType() const
{
    return diagramTypeSelector.getSelectedId() == 2 ? "state_machine" : "node_graph";
}

juce::String NodeDesignerPanel::selectedTarget() const
{
    return targetSelector.getSelectedId() == 2 ? "glsl" : "frust";
}

juce::String NodeDesignerPanel::selectedFrustProjectType() const
{
    return frustProjectTypeSelector.getSelectedId() == 2 ? "lib" : "bin";
}

void NodeDesignerPanel::refreshProperties()
{
    if (inspector != nullptr)
        inspector->refreshFromModel();
}

void NodeDesignerPanel::saveGraph()
{
    if (currentGraphFile == juce::File())
    {
        fileChooser = std::make_unique<juce::FileChooser>("Save Node Schematic", juce::File::getCurrentWorkingDirectory().getChildFile("graph.frnode.json"), "*.frnode.json;*.json");
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& chooser) {
                currentGraphFile = chooser.getResult();
                if (currentGraphFile != juce::File()) saveGraph();
                fileChooser = nullptr;
            });
        return;
    }
    if (!currentGraphFile.replaceWithText(buildSchematicJson()))
    {
        setStatus("Could not save " + currentGraphFile.getFullPathName(), true);
        return;
    }
    currentSourceFile = currentGraphFile.withFileExtension(".fr");
    titleLabel.setText(currentGraphFile.getFileName(), juce::dontSendNotification);
    setStatus("Saved schematic.");
}

void NodeDesignerPanel::compileGraph()
{
    if (diagramType == "state_machine")
    {
        generatedSource.clear();
        generatedFiles.clear();
        const auto validation = validateStateMachine();
        if (validation.isNotEmpty())
        {
            setStatus("State-machine validation failed: " + validation, true);
            return;
        }

        const auto schematicJson = buildSchematicJson(false);
        const auto result = node_compiler::CompileSchematic(schematicJson.toStdString());
        if (!result.ok)
        {
            setStatus(juce::String(result.errorMessage), true);
            return;
        }

        generatedSource = juce::String(result.source);
        generatedFiles = result.files;
        setStatus("Compiled state-machine schematic to Frust. JSON remains authoritative; generated files are build artifacts.");
        return;
    }

    if (selectedTarget() != "frust")
    {
        generatedSource.clear();
        generatedFiles.clear();
        setStatus("GLSL is a production reference target. This research canvas currently compiles Frust.", true);
        return;
    }

    const auto schematicJson = buildSchematicJson(false);
    const auto result = node_compiler::CompileSchematic(schematicJson.toStdString());
    if (!result.ok)
    {
        generatedSource.clear();
        generatedFiles.clear();
        setStatus(juce::String(result.errorMessage), true);
        return;
    }

    generatedSource = juce::String(result.source);
    generatedFiles = result.files;

    juce::String artifact = "function source";
    if (result.artifactKind == node_compiler::ArtifactKind::FrustExecutablePod)
        artifact = "Frust executable pod";
    else if (result.artifactKind == node_compiler::ArtifactKind::FrustLibraryPod)
        artifact = "Frust library pod";

    setStatus("Compiled node schematic as " + artifact + ". JSON remains authoritative; generated files are build artifacts.");
}

void NodeDesignerPanel::saveGeneratedSource()
{
    if (generatedSource.trim().isEmpty() && generatedFiles.empty())
    {
        setStatus("Compile the schematic before exporting generated Frust source.", true);
        return;
    }

    if (!generatedFiles.empty())
    {
        const auto packageRoot = generatedSourceCacheFile().getParentDirectory();
        for (const auto& generated : generatedFiles)
        {
            auto file = packageRoot.getChildFile(juce::String(generated.path));
            file.getParentDirectory().createDirectory();
            if (!file.replaceWithText(juce::String(generated.content)))
            {
                setStatus("Could not save " + file.getFullPathName(), true);
                return;
            }
            if (generated.path == "src/main.fr" || generated.path == "src/lib.fr")
                currentSourceFile = file;
        }
        setStatus("Exported generated artifact package to " + packageRoot.getFullPathName());
        return;
    }

    currentSourceFile = generatedSourceCacheFile();
    currentSourceFile.getParentDirectory().createDirectory();

    if (!currentSourceFile.replaceWithText(generatedSource))
    {
        setStatus("Could not save " + currentSourceFile.getFullPathName(), true);
        return;
    }
    setStatus("Exported generated source artifact to " + currentSourceFile.getFullPathName());
}

void NodeDesignerPanel::setStatus(const juce::String& text, bool isError)
{
    statusView.setColour(juce::TextEditor::textColourId, isError ? juce::Colour(0xffff8b8b) : juce::Colour(0xffdce9ee));
    statusView.setText(text, juce::dontSendNotification);
}
