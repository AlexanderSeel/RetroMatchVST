#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace PatchGraph
{
static constexpr int schemaVersion = 1;

enum class PortType : int
{
    audio = 0,
    modulation,
    clock,
    control
};

enum class PortDirection : int
{
    input = 0,
    output
};

enum class NodeType : int
{
    source = 0,
    processor,
    mixer,
    modulationRouter,
    clock,
    master,
    utility
};

struct Port
{
    juce::String id;
    PortType type = PortType::audio;
    PortDirection direction = PortDirection::input;
    bool acceptsMultiple = false;
    bool modulationSafe = false;
};

struct Position
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Node
{
    juce::String id;
    NodeType type = NodeType::processor;
    juce::String title;
    std::vector<Port> ports;
    Position position;
    bool positionValid = false;
    bool locked = false;

    const Port* findPort (const juce::String& portId) const noexcept
    {
        for (const auto& port : ports)
            if (port.id == portId) return &port;
        return nullptr;
    }
};

struct Edge
{
    juce::String id;
    juce::String fromNode;
    juce::String fromPort;
    juce::String toNode;
    juce::String toPort;
    PortType type = PortType::audio;
    bool editable = false;
};

struct ViewState
{
    Position pan { 18.0f, 18.0f };
    float zoom = 0.82f;
    bool snapToGrid = true;
};

struct ValidationResult
{
    bool ok = true;
    juce::String message;
    juce::String edgeId;

    static ValidationResult success() { return {}; }
    static ValidationResult failure (juce::String text, juce::String edge = {})
    {
        ValidationResult result;
        result.ok = false;
        result.message = std::move (text);
        result.edgeId = std::move (edge);
        return result;
    }
};

class Document
{
public:
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    ViewState view;
    int version = schemaVersion;

    void clearTopology()
    {
        nodes.clear();
        edges.clear();
    }

    const Node* findNode (const juce::String& nodeId) const noexcept
    {
        for (const auto& node : nodes)
            if (node.id == nodeId) return &node;
        return nullptr;
    }

    Node* findNode (const juce::String& nodeId) noexcept
    {
        for (auto& node : nodes)
            if (node.id == nodeId) return &node;
        return nullptr;
    }

    const Edge* findEdge (const juce::String& edgeId) const noexcept
    {
        for (const auto& edge : edges)
            if (edge.id == edgeId) return &edge;
        return nullptr;
    }

    Edge* findEdge (const juce::String& edgeId) noexcept
    {
        for (auto& edge : edges)
            if (edge.id == edgeId) return &edge;
        return nullptr;
    }

    bool addNode (Node node, juce::String* reason = nullptr)
    {
        if (node.id.isEmpty())
        {
            if (reason != nullptr) *reason = "Node ID must not be empty";
            return false;
        }
        if (findNode (node.id) != nullptr)
        {
            if (reason != nullptr) *reason = "Duplicate node ID: " + node.id;
            return false;
        }

        std::set<std::string> portIds;
        for (const auto& port : node.ports)
        {
            if (port.id.isEmpty() || ! portIds.insert (port.id.toStdString()).second)
            {
                if (reason != nullptr) *reason = "Invalid or duplicate port on " + node.id;
                return false;
            }
        }
        nodes.push_back (std::move (node));
        return true;
    }

    ValidationResult validateConnection (const Edge& candidate) const
    {
        const auto* fromNode = findNode (candidate.fromNode);
        const auto* toNode = findNode (candidate.toNode);
        if (fromNode == nullptr || toNode == nullptr)
            return ValidationResult::failure ("Connection references a missing node", candidate.id);
        if (candidate.fromNode == candidate.toNode)
            return ValidationResult::failure ("A node cannot connect directly to itself", candidate.id);

        const auto* fromPort = fromNode->findPort (candidate.fromPort);
        const auto* toPort = toNode->findPort (candidate.toPort);
        if (fromPort == nullptr || toPort == nullptr)
            return ValidationResult::failure ("Connection references a missing port", candidate.id);
        if (fromPort->direction != PortDirection::output || toPort->direction != PortDirection::input)
            return ValidationResult::failure ("Connections must run from an output port to an input port", candidate.id);
        if (fromPort->type != toPort->type || fromPort->type != candidate.type)
            return ValidationResult::failure ("Port types are incompatible", candidate.id);
        if (candidate.type == PortType::modulation && ! toPort->modulationSafe)
            return ValidationResult::failure ("Target is not modulation-safe", candidate.id);

        for (const auto& edge : edges)
        {
            if (! toPort->acceptsMultiple
                && edge.fromNode == candidate.fromNode && edge.fromPort == candidate.fromPort
                && edge.toNode == candidate.toNode && edge.toPort == candidate.toPort)
                return ValidationResult::failure ("Connection already exists", candidate.id);
            if (! toPort->acceptsMultiple && edge.toNode == candidate.toNode && edge.toPort == candidate.toPort)
                return ValidationResult::failure ("Target input already has a connection", candidate.id);
        }

        if (candidate.type == PortType::audio && wouldCreateAudioCycle (candidate))
            return ValidationResult::failure ("Audio connection would create a zero-delay cycle", candidate.id);
        return ValidationResult::success();
    }

    bool addEdge (Edge edge, juce::String* reason = nullptr)
    {
        if (edge.id.isEmpty())
            edge.id = edge.fromNode + ":" + edge.fromPort + ">" + edge.toNode + ":" + edge.toPort;
        for (const auto& existing : edges)
            if (existing.id == edge.id)
            {
                if (reason != nullptr) *reason = "Duplicate edge ID: " + edge.id;
                return false;
            }

        const auto result = validateConnection (edge);
        if (! result.ok)
        {
            if (reason != nullptr) *reason = result.message;
            return false;
        }
        edges.push_back (std::move (edge));
        return true;
    }

    bool removeEdge (const juce::String& edgeId, juce::String* reason = nullptr, bool requireValidGraph = true)
    {
        Document candidate = *this;
        const auto it = std::find_if (candidate.edges.begin(), candidate.edges.end(), [&] (const Edge& edge) { return edge.id == edgeId; });
        if (it == candidate.edges.end())
        {
            if (reason != nullptr) *reason = "Connection not found: " + edgeId;
            return false;
        }
        candidate.edges.erase (it);
        if (requireValidGraph)
        {
            const auto validation = candidate.validate();
            if (! validation.ok)
            {
                if (reason != nullptr) *reason = validation.message;
                return false;
            }
        }
        *this = std::move (candidate);
        return true;
    }

    bool replaceEdge (const juce::String& edgeId, Edge replacement, juce::String* reason = nullptr)
    {
        Document candidate = *this;
        const auto it = std::find_if (candidate.edges.begin(), candidate.edges.end(), [&] (const Edge& edge) { return edge.id == edgeId; });
        if (it == candidate.edges.end())
        {
            if (reason != nullptr) *reason = "Connection not found: " + edgeId;
            return false;
        }
        candidate.edges.erase (it);
        if (replacement.id.isEmpty()) replacement.id = edgeId;
        juce::String localReason;
        if (! candidate.addEdge (std::move (replacement), &localReason))
        {
            if (reason != nullptr) *reason = localReason;
            return false;
        }
        const auto validation = candidate.validate();
        if (! validation.ok)
        {
            if (reason != nullptr) *reason = validation.message;
            return false;
        }
        *this = std::move (candidate);
        return true;
    }

    ValidationResult validate() const
    {
        Document rebuilt;
        rebuilt.view = view;
        rebuilt.version = version;
        for (const auto& node : nodes)
        {
            juce::String reason;
            if (! rebuilt.addNode (node, &reason)) return ValidationResult::failure (reason);
        }
        for (const auto& edge : edges)
        {
            juce::String reason;
            if (! rebuilt.addEdge (edge, &reason)) return ValidationResult::failure (reason, edge.id);
        }

        bool hasAudioNode = false;
        bool hasMaster = false;
        for (const auto& node : nodes)
        {
            bool hasAudioOutput = false;
            for (const auto& port : node.ports)
                hasAudioOutput |= port.type == PortType::audio && port.direction == PortDirection::output;
            hasAudioNode |= hasAudioOutput;
            hasMaster |= node.type == NodeType::master;
            if (hasAudioOutput && node.type != NodeType::master && ! canReachMaster (node.id))
                return ValidationResult::failure ("Audio node does not resolve to MASTER OUT: " + node.id);
        }
        if (hasAudioNode && ! hasMaster)
            return ValidationResult::failure ("Audio graph has no MASTER OUT node");
        return ValidationResult::success();
    }

    std::vector<juce::String> topologicalOrder (PortType type = PortType::audio) const
    {
        std::map<std::string, int> indegree;
        std::map<std::string, std::vector<std::string>> outgoing;
        for (const auto& node : nodes)
        {
            bool participates = false;
            for (const auto& port : node.ports) participates |= port.type == type;
            if (participates) indegree[node.id.toStdString()] = 0;
        }
        for (const auto& edge : edges)
        {
            if (edge.type != type) continue;
            const auto from = edge.fromNode.toStdString(), to = edge.toNode.toStdString();
            if (indegree.find (from) == indegree.end() || indegree.find (to) == indegree.end()) continue;
            outgoing[from].push_back (to);
            ++indegree[to];
        }

        std::vector<std::string> ready;
        for (const auto& [id, degree] : indegree)
            if (degree == 0) ready.push_back (id);
        std::sort (ready.begin(), ready.end());

        std::vector<juce::String> order;
        while (! ready.empty())
        {
            const auto id = ready.front();
            ready.erase (ready.begin());
            order.emplace_back (id);
            auto successors = outgoing[id];
            std::sort (successors.begin(), successors.end());
            for (const auto& next : successors)
            {
                if (--indegree[next] == 0)
                {
                    ready.push_back (next);
                    std::sort (ready.begin(), ready.end());
                }
            }
        }
        return order;
    }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree root ("PATCH_GRAPH");
        root.setProperty ("version", version, nullptr);
        root.setProperty ("panX", view.pan.x, nullptr);
        root.setProperty ("panY", view.pan.y, nullptr);
        root.setProperty ("zoom", view.zoom, nullptr);
        root.setProperty ("snapToGrid", view.snapToGrid, nullptr);

        juce::ValueTree nodeTree ("NODES");
        for (const auto& node : nodes)
        {
            juce::ValueTree child ("NODE");
            child.setProperty ("id", node.id, nullptr);
            child.setProperty ("type", (int) node.type, nullptr);
            child.setProperty ("title", node.title, nullptr);
            child.setProperty ("positionValid", node.positionValid, nullptr);
            child.setProperty ("x", node.position.x, nullptr);
            child.setProperty ("y", node.position.y, nullptr);
            child.setProperty ("locked", node.locked, nullptr);
            for (const auto& port : node.ports)
            {
                juce::ValueTree p ("PORT");
                p.setProperty ("id", port.id, nullptr);
                p.setProperty ("type", (int) port.type, nullptr);
                p.setProperty ("direction", (int) port.direction, nullptr);
                p.setProperty ("multiple", port.acceptsMultiple, nullptr);
                p.setProperty ("modulationSafe", port.modulationSafe, nullptr);
                child.appendChild (p, nullptr);
            }
            nodeTree.appendChild (child, nullptr);
        }
        root.appendChild (nodeTree, nullptr);

        juce::ValueTree edgeTree ("EDGES");
        for (const auto& edge : edges)
        {
            juce::ValueTree child ("EDGE");
            child.setProperty ("id", edge.id, nullptr);
            child.setProperty ("fromNode", edge.fromNode, nullptr);
            child.setProperty ("fromPort", edge.fromPort, nullptr);
            child.setProperty ("toNode", edge.toNode, nullptr);
            child.setProperty ("toPort", edge.toPort, nullptr);
            child.setProperty ("type", (int) edge.type, nullptr);
            child.setProperty ("editable", edge.editable, nullptr);
            edgeTree.appendChild (child, nullptr);
        }
        root.appendChild (edgeTree, nullptr);
        return root;
    }

    static Document fromValueTree (const juce::ValueTree& root, juce::String* repairNotes = nullptr)
    {
        Document result;
        if (! root.isValid() || ! root.hasType ("PATCH_GRAPH")) return result;
        result.version = juce::jmax (1, (int) root.getProperty ("version", 1));
        result.view.pan.x = (float) root.getProperty ("panX", 18.0f);
        result.view.pan.y = (float) root.getProperty ("panY", 18.0f);
        result.view.zoom = juce::jlimit (0.35f, 2.2f, (float) root.getProperty ("zoom", 0.82f));
        result.view.snapToGrid = (bool) root.getProperty ("snapToGrid", true);

        auto appendRepair = [repairNotes] (const juce::String& text)
        {
            if (repairNotes == nullptr) return;
            if (repairNotes->isNotEmpty()) *repairNotes += "\n";
            *repairNotes += text;
        };

        const auto nodeTree = root.getChildWithName ("NODES");
        for (const auto& child : nodeTree)
        {
            if (! child.hasType ("NODE")) continue;
            Node node;
            node.id = child["id"].toString();
            node.type = (NodeType) juce::jlimit (0, (int) NodeType::utility, (int) child.getProperty ("type", (int) NodeType::processor));
            node.title = child["title"].toString();
            node.positionValid = (bool) child.getProperty ("positionValid", false);
            node.position = { (float) child.getProperty ("x", 0.0f), (float) child.getProperty ("y", 0.0f) };
            node.locked = (bool) child.getProperty ("locked", false);
            for (const auto& portChild : child)
            {
                if (! portChild.hasType ("PORT")) continue;
                Port port;
                port.id = portChild["id"].toString();
                port.type = (PortType) juce::jlimit (0, (int) PortType::control, (int) portChild.getProperty ("type", 0));
                port.direction = (PortDirection) juce::jlimit (0, 1, (int) portChild.getProperty ("direction", 0));
                port.acceptsMultiple = (bool) portChild.getProperty ("multiple", false);
                port.modulationSafe = (bool) portChild.getProperty ("modulationSafe", false);
                node.ports.push_back (std::move (port));
            }
            juce::String reason;
            if (! result.addNode (std::move (node), &reason)) appendRepair (reason);
        }

        const auto edgeTree = root.getChildWithName ("EDGES");
        for (const auto& child : edgeTree)
        {
            if (! child.hasType ("EDGE")) continue;
            Edge edge;
            edge.id = child["id"].toString();
            edge.fromNode = child["fromNode"].toString();
            edge.fromPort = child["fromPort"].toString();
            edge.toNode = child["toNode"].toString();
            edge.toPort = child["toPort"].toString();
            edge.type = (PortType) juce::jlimit (0, (int) PortType::control, (int) child.getProperty ("type", 0));
            edge.editable = (bool) child.getProperty ("editable", false);
            juce::String reason;
            if (! result.addEdge (std::move (edge), &reason)) appendRepair (reason);
        }
        return result;
    }

    juce::String fingerprint() const
    {
        juce::String out;
        out << "v" << version << "|view:" << juce::String (view.pan.x, 3) << "," << juce::String (view.pan.y, 3)
            << "," << juce::String (view.zoom, 4) << "," << (view.snapToGrid ? 1 : 0);
        for (const auto& node : nodes)
        {
            out << "|n:" << node.id << ":" << (int) node.type << ":" << (node.positionValid ? 1 : 0)
                << ":" << juce::String (node.position.x, 3) << ":" << juce::String (node.position.y, 3) << ":" << (node.locked ? 1 : 0);
            for (const auto& port : node.ports)
                out << "/p:" << port.id << ":" << (int) port.type << ":" << (int) port.direction
                    << ":" << (port.acceptsMultiple ? 1 : 0) << ":" << (port.modulationSafe ? 1 : 0);
        }
        for (const auto& edge : edges)
            out << "|e:" << edge.id << ":" << edge.fromNode << ":" << edge.fromPort << ":"
                << edge.toNode << ":" << edge.toPort << ":" << (int) edge.type << ":" << (edge.editable ? 1 : 0);
        return out;
    }

private:
    bool wouldCreateAudioCycle (const Edge& candidate) const
    {
        if (candidate.type != PortType::audio) return false;
        std::map<std::string, std::vector<std::string>> outgoing;
        for (const auto& edge : edges)
            if (edge.type == PortType::audio) outgoing[edge.fromNode.toStdString()].push_back (edge.toNode.toStdString());
        outgoing[candidate.fromNode.toStdString()].push_back (candidate.toNode.toStdString());

        const auto target = candidate.fromNode.toStdString();
        std::vector<std::string> stack { candidate.toNode.toStdString() };
        std::set<std::string> visited;
        while (! stack.empty())
        {
            auto current = stack.back();
            stack.pop_back();
            if (current == target) return true;
            if (! visited.insert (current).second) continue;
            for (const auto& next : outgoing[current]) stack.push_back (next);
        }
        return false;
    }

    bool canReachMaster (const juce::String& startNode) const
    {
        std::map<std::string, std::vector<std::string>> outgoing;
        for (const auto& edge : edges)
            if (edge.type == PortType::audio) outgoing[edge.fromNode.toStdString()].push_back (edge.toNode.toStdString());

        std::vector<std::string> stack { startNode.toStdString() };
        std::set<std::string> visited;
        while (! stack.empty())
        {
            auto current = stack.back();
            stack.pop_back();
            if (! visited.insert (current).second) continue;
            if (const auto* node = findNode (juce::String (current)); node != nullptr && node->type == NodeType::master) return true;
            for (const auto& next : outgoing[current]) stack.push_back (next);
        }
        return false;
    }
};

inline Port audioInput (bool acceptsMultiple = false) { return { "audio.in", PortType::audio, PortDirection::input, acceptsMultiple, false }; }
inline Port audioOutput() { return { "audio.out", PortType::audio, PortDirection::output, true, false }; }
inline Port modulationInput (bool acceptsMultiple = true) { return { "mod.in", PortType::modulation, PortDirection::input, acceptsMultiple, true }; }
inline Port modulationOutput() { return { "mod.out", PortType::modulation, PortDirection::output, true, false }; }
inline Port clockInput (bool acceptsMultiple = false) { return { "clock.in", PortType::clock, PortDirection::input, acceptsMultiple, false }; }
inline Port clockOutput() { return { "clock.out", PortType::clock, PortDirection::output, true, false }; }
}
