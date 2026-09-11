#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace PatchGraph
{
static constexpr int schemaVersion = 3;

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
    int routingMode = 0;
    bool solo = false;
    // Declared processing latency in samples. Mutable graph state is compiled
    // before publication; the audio thread never reads Document directly.
    int latencySamples = 0;

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
    int sequence = -1;
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
        if (node.latencySamples < 0 || node.latencySamples > (1 << 20))
        {
            if (reason != nullptr) *reason = "Node latency is outside the supported range: " + node.id;
            return false;
        }
        if (node.positionValid && (! std::isfinite (node.position.x) || ! std::isfinite (node.position.y)
                                   || std::abs (node.position.x) > 100000.0f || std::abs (node.position.y) > 100000.0f))
        {
            if (reason != nullptr) *reason = "Node position is outside the supported range: " + node.id;
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

    bool setNodeLocked (const juce::String& nodeId, bool locked, juce::String* reason = nullptr)
    {
        Document candidate = *this;
        auto* node = candidate.findNode (nodeId);
        if (node == nullptr)
        {
            if (reason != nullptr) *reason = "Node not found: " + nodeId;
            return false;
        }
        node->locked = locked;
        const auto validation = candidate.validate();
        if (! validation.ok)
        {
            if (reason != nullptr) *reason = validation.message;
            return false;
        }
        *this = std::move (candidate);
        return true;
    }

    bool setNodePosition (const juce::String& nodeId, Position position, juce::String* reason = nullptr)
    {
        if (! std::isfinite (position.x) || ! std::isfinite (position.y))
        {
            if (reason != nullptr) *reason = "Node position must be finite: " + nodeId;
            return false;
        }
        auto* node = findNode (nodeId);
        if (node == nullptr)
        {
            if (reason != nullptr) *reason = "Node not found: " + nodeId;
            return false;
        }
        if (node->locked && (node->position.x != position.x || node->position.y != position.y))
        {
            if (reason != nullptr) *reason = "Node position is locked: " + nodeId;
            return false;
        }
        node->position = position;
        node->positionValid = true;
        return true;
    }

    bool setNodeRoutingMode (const juce::String& nodeId, int mode, juce::String* reason = nullptr)
    {
        if (mode < 0 || mode > 1)
        {
            if (reason != nullptr) *reason = "Unsupported routing mode: " + juce::String (mode);
            return false;
        }
        Document candidate = *this;
        auto* node = candidate.findNode (nodeId);
        if (node == nullptr)
        {
            if (reason != nullptr) *reason = "Node not found: " + nodeId;
            return false;
        }
        if (node->type != NodeType::processor)
        {
            if (reason != nullptr) *reason = "Only processor nodes own serial/parallel FX routing";
            return false;
        }
        node->routingMode = mode;
        const auto validation = candidate.validate();
        if (! validation.ok)
        {
            if (reason != nullptr) *reason = validation.message;
            return false;
        }
        *this = std::move (candidate);
        return true;
    }

    bool setNodeLatencySamples (const juce::String& nodeId, int samples, juce::String* reason = nullptr)
    {
        if (samples < 0 || samples > (1 << 20))
        {
            if (reason != nullptr) *reason = "Node latency is outside the supported range";
            return false;
        }
        Document candidate = *this;
        auto* node = candidate.findNode (nodeId);
        if (node == nullptr)
        {
            if (reason != nullptr) *reason = "Node not found: " + nodeId;
            return false;
        }
        node->latencySamples = samples;
        const auto validation = candidate.validate();
        if (! validation.ok)
        {
            if (reason != nullptr) *reason = validation.message;
            return false;
        }
        *this = std::move (candidate);
        return true;
    }

    bool setNodeSolo (const juce::String& nodeId, bool enabled, juce::String* reason = nullptr)
    {
        Document candidate = *this;
        auto* node = candidate.findNode (nodeId);
        if (node == nullptr)
        {
            if (reason != nullptr) *reason = "Node not found: " + nodeId;
            return false;
        }
        if (node->type != NodeType::source)
        {
            if (reason != nullptr) *reason = "Only source nodes can be soloed";
            return false;
        }
        for (auto& item : candidate.nodes) item.solo = false;
        node->solo = enabled;
        const auto validation = candidate.validate();
        if (! validation.ok)
        {
            if (reason != nullptr) *reason = validation.message;
            return false;
        }
        *this = std::move (candidate);
        return true;
    }

    bool disconnectEdge (const juce::String& edgeId, Edge* removed = nullptr,
                         juce::String* reason = nullptr, bool requireValidGraph = true)
    {
        Document candidate = *this;
        const auto it = std::find_if (candidate.edges.begin(), candidate.edges.end(),
                                      [&] (const Edge& edge) { return edge.id == edgeId; });
        if (it == candidate.edges.end())
        {
            if (reason != nullptr) *reason = "Connection not found: " + edgeId;
            return false;
        }
        if (! it->editable)
        {
            if (reason != nullptr) *reason = "Connection is owned by the fixed engine: " + edgeId;
            return false;
        }
        if (removed != nullptr) *removed = *it;
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

    bool restoreEdge (Edge edge, juce::String* reason = nullptr)
    {
        Document candidate = *this;
        if (! candidate.addEdge (std::move (edge), reason)) return false;
        const auto validation = candidate.validate();
        if (! validation.ok)
        {
            if (reason != nullptr) *reason = validation.message;
            return false;
        }
        *this = std::move (candidate);
        return true;
    }

    bool removeNode (const juce::String& nodeId, juce::String* reason = nullptr)
    {
        Document candidate = *this;
        const auto it = std::find_if (candidate.nodes.begin(), candidate.nodes.end(),
                                      [&] (const Node& node) { return node.id == nodeId; });
        if (it == candidate.nodes.end())
        {
            if (reason != nullptr) *reason = "Node not found: " + nodeId;
            return false;
        }
        if (it->locked || it->type == NodeType::master || it->type == NodeType::clock)
        {
            if (reason != nullptr) *reason = "System or locked nodes cannot be removed: " + nodeId;
            return false;
        }
        std::vector<Edge> incoming, outgoing;
        for (const auto& edge : candidate.edges)
        {
            if (edge.toNode == nodeId) incoming.push_back (edge);
            if (edge.fromNode == nodeId) outgoing.push_back (edge);
        }
        if (incoming.size() == 1 && outgoing.size() == 1
            && incoming.front().type == PortType::audio && outgoing.front().type == PortType::audio)
        {
            const auto& in = incoming.front();
            const auto& out = outgoing.front();
            Edge bypass;
            bypass.id = in.id + ":bypass";
            bypass.fromNode = in.fromNode; bypass.fromPort = in.fromPort;
            bypass.toNode = out.toNode; bypass.toPort = out.toPort;
            bypass.type = PortType::audio;
            bypass.editable = in.editable || out.editable;
            bypass.sequence = in.sequence >= 0 ? in.sequence : out.sequence;
            candidate.edges.erase (std::remove_if (candidate.edges.begin(), candidate.edges.end(),
                [&] (const Edge& edge) { return edge.fromNode == nodeId || edge.toNode == nodeId; }), candidate.edges.end());
            candidate.nodes.erase (it);
            juce::String bypassReason;
            if (! candidate.addEdge (std::move (bypass), &bypassReason))
            {
                if (reason != nullptr) *reason = "Removing the node would create an invalid bypass: " + bypassReason;
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
        if (! incoming.empty() || ! outgoing.empty())
        {
            if (reason != nullptr) *reason = "Node removal requires one audio input and one audio output so the route can be bypassed";
            return false;
        }
        candidate.edges.erase (std::remove_if (candidate.edges.begin(), candidate.edges.end(),
            [&] (const Edge& edge) { return edge.fromNode == nodeId || edge.toNode == nodeId; }), candidate.edges.end());
        candidate.nodes.erase (it);
        const auto validation = candidate.validate();
        if (! validation.ok)
        {
            if (reason != nullptr) *reason = validation.message;
            return false;
        }
        *this = std::move (candidate);
        return true;
    }

    bool insertNodeOnEdge (const juce::String& edgeId, Node node, juce::String* reason = nullptr)
    {
        Document candidate = *this;
        const auto old = std::find_if (candidate.edges.begin(), candidate.edges.end(),
                                       [&] (const Edge& edge) { return edge.id == edgeId; });
        if (old == candidate.edges.end())
        {
            if (reason != nullptr) *reason = "Connection not found: " + edgeId;
            return false;
        }
        if (! old->editable || old->type != PortType::audio)
        {
            if (reason != nullptr) *reason = "Only editable audio connections can accept an inserted node";
            return false;
        }
        if (node.id.isEmpty() || candidate.findNode (node.id) != nullptr)
        {
            if (reason != nullptr) *reason = "Inserted node ID is empty or already exists";
            return false;
        }
        if (node.findPort ("audio.in") == nullptr || node.findPort ("audio.out") == nullptr)
        {
            if (reason != nullptr) *reason = "Inserted node must expose audio.in and audio.out";
            return false;
        }

        const Edge original = *old;
        candidate.edges.erase (old);
        juce::String addReason;
        if (! candidate.addNode (std::move (node), &addReason))
        {
            if (reason != nullptr) *reason = addReason;
            return false;
        }

        Edge incoming = original;
        incoming.id = edgeId + ":in";
        incoming.toNode = candidate.nodes.back().id;
        incoming.toPort = "audio.in";
        incoming.editable = true;
        Edge outgoing = original;
        outgoing.id = edgeId + ":out";
        outgoing.fromNode = candidate.nodes.back().id;
        outgoing.fromPort = "audio.out";
        outgoing.editable = true;
        if (! candidate.addEdge (std::move (incoming), &addReason)
            || ! candidate.addEdge (std::move (outgoing), &addReason))
        {
            if (reason != nullptr) *reason = addReason;
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

    bool mergeEdges (const juce::String& firstEdgeId, const juce::String& secondEdgeId,
                     Node mergeNode, juce::String* reason = nullptr)
    {
        if (firstEdgeId == secondEdgeId)
        {
            if (reason != nullptr) *reason = "Merge requires two different connections";
            return false;
        }
        Document candidate = *this;
        const auto first = std::find_if (candidate.edges.begin(), candidate.edges.end(),
                                         [&] (const Edge& edge) { return edge.id == firstEdgeId; });
        const auto second = std::find_if (candidate.edges.begin(), candidate.edges.end(),
                                          [&] (const Edge& edge) { return edge.id == secondEdgeId; });
        if (first == candidate.edges.end() || second == candidate.edges.end())
        {
            if (reason != nullptr) *reason = "Merge connection not found";
            return false;
        }
        if (! first->editable || ! second->editable || first->type != PortType::audio || second->type != PortType::audio
            || first->toNode != second->toNode || first->toPort != second->toPort)
        {
            if (reason != nullptr) *reason = "Merge requires two editable audio connections sharing a fan-in";
            return false;
        }
        const auto* destination = candidate.findNode (first->toNode);
        const auto* destinationPort = destination != nullptr ? destination->findPort (first->toPort) : nullptr;
        if (destinationPort == nullptr || ! destinationPort->acceptsMultiple)
        {
            if (reason != nullptr) *reason = "Merge destination does not accept multiple audio sources";
            return false;
        }
        if (mergeNode.id.isEmpty() || candidate.findNode (mergeNode.id) != nullptr
            || mergeNode.findPort ("audio.in.a") == nullptr
            || mergeNode.findPort ("audio.in.b") == nullptr
            || mergeNode.findPort ("audio.out") == nullptr)
        {
            if (reason != nullptr) *reason = "Merge node must be unique and expose audio.in.a, audio.in.b and audio.out";
            return false;
        }

        const Edge firstOriginal = *first;
        const Edge secondOriginal = *second;
        candidate.edges.erase (std::remove_if (candidate.edges.begin(), candidate.edges.end(),
            [&] (const Edge& edge) { return edge.id == firstEdgeId || edge.id == secondEdgeId; }), candidate.edges.end());
        juce::String addReason;
        if (! candidate.addNode (std::move (mergeNode), &addReason))
        {
            if (reason != nullptr) *reason = addReason;
            return false;
        }
        const auto mergeId = candidate.nodes.back().id;
        Edge incomingA = firstOriginal;
        incomingA.id = firstEdgeId + ":merge"; incomingA.toNode = mergeId; incomingA.toPort = "audio.in.a";
        Edge incomingB = secondOriginal;
        incomingB.id = secondEdgeId + ":merge"; incomingB.toNode = mergeId; incomingB.toPort = "audio.in.b";
        Edge outgoing = firstOriginal;
        outgoing.id = firstEdgeId + ":merged"; outgoing.fromNode = mergeId; outgoing.fromPort = "audio.out";
        if (! candidate.addEdge (std::move (incomingA), &addReason)
            || ! candidate.addEdge (std::move (incomingB), &addReason)
            || ! candidate.addEdge (std::move (outgoing), &addReason))
        {
            if (reason != nullptr) *reason = addReason;
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

    // Reorder only an explicitly editable fan-in. The operation is model-level
    // and therefore gives UI undo/redo and the DSP compiler the same ordering
    // contract; arbitrary serial processing cannot be reordered accidentally.
    bool reorderEdge (const juce::String& edgeId, int newPosition, juce::String* reason = nullptr)
    {
        Document candidate = *this;
        const auto target = std::find_if (candidate.edges.begin(), candidate.edges.end(),
                                          [&] (const Edge& edge) { return edge.id == edgeId; });
        if (target == candidate.edges.end())
        {
            if (reason != nullptr) *reason = "Connection not found: " + edgeId;
            return false;
        }
        if (! target->editable)
        {
            if (reason != nullptr) *reason = "Connection is owned by the fixed engine: " + edgeId;
            return false;
        }
        if (target->type != PortType::audio)
        {
            if (reason != nullptr) *reason = "Only editable audio fan-ins have a processing order";
            return false;
        }
        const auto* destination = candidate.findNode (target->toNode);
        const auto* port = destination != nullptr ? destination->findPort (target->toPort) : nullptr;
        if (port == nullptr || ! port->acceptsMultiple)
        {
            if (reason != nullptr) *reason = "The destination does not expose an editable fan-in";
            return false;
        }

        std::vector<size_t> peers;
        for (size_t index = 0; index < candidate.edges.size(); ++index)
        {
            const auto& edge = candidate.edges[index];
            if (edge.editable && edge.type == PortType::audio
                && edge.toNode == target->toNode && edge.toPort == target->toPort)
                peers.push_back (index);
        }
        if (peers.size() < 2)
        {
            if (reason != nullptr) *reason = "At least two editable fan-in connections are required";
            return false;
        }

        std::stable_sort (peers.begin(), peers.end(), [&] (size_t a, size_t b)
        {
            const auto sequence = [&] (size_t index) {
                const auto value = candidate.edges[index].sequence;
                return value >= 0 ? value : static_cast<int> (index);
            };
            return sequence (a) < sequence (b);
        });
        const auto targetIt = std::find (peers.begin(), peers.end(), (size_t) std::distance (candidate.edges.begin(), target));
        if (targetIt == peers.end()) return false;
        const auto moved = *targetIt;
        peers.erase (targetIt);
        const int clampedPosition = juce::jlimit (0, static_cast<int> (peers.size()), newPosition);
        peers.insert (peers.begin() + clampedPosition, moved);
        for (int sequence = 0; sequence < static_cast<int> (peers.size()); ++sequence)
            candidate.edges[peers[(size_t) sequence]].sequence = sequence;

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
        if (! std::isfinite (view.pan.x) || ! std::isfinite (view.pan.y)
            || std::abs (view.pan.x) > 100000.0f || std::abs (view.pan.y) > 100000.0f
            || ! std::isfinite (view.zoom) || view.zoom < 0.05f || view.zoom > 8.0f)
            return ValidationResult::failure ("Patch graph view state is not finite or bounded");

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
        int soloCount = 0;
        for (const auto& node : nodes)
            soloCount += node.solo ? 1 : 0;
        if (soloCount > 1)
            return ValidationResult::failure ("Patch graph contains more than one solo source");
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

    int maxAudioLatencySamples() const noexcept
    {
        const auto order = topologicalOrder (PortType::audio);
        std::map<std::string, int> accumulated;
        int maximum = 0;
        for (const auto& nodeId : order)
        {
            const auto* node = findNode (nodeId);
            if (node == nullptr) continue;

            int inputLatency = 0;
            for (const auto& edge : edges)
                if (edge.type == PortType::audio && edge.toNode == nodeId)
                    inputLatency = juce::jmax (inputLatency, accumulated[edge.fromNode.toStdString()]);

            const int pathLatency = inputLatency + node->latencySamples;
            accumulated[nodeId.toStdString()] = pathLatency;
            maximum = juce::jmax (maximum, pathLatency);
        }
        return maximum;
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
            child.setProperty ("routingMode", node.routingMode, nullptr);
            child.setProperty ("solo", node.solo, nullptr);
            child.setProperty ("latencySamples", node.latencySamples, nullptr);
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
            child.setProperty ("sequence", edge.sequence, nullptr);
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
            node.routingMode = juce::jlimit (0, 1, (int) child.getProperty ("routingMode", 0));
            node.solo = (bool) child.getProperty ("solo", false);
            node.latencySamples = juce::jlimit (0, 1 << 20, (int) child.getProperty ("latencySamples", 0));
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
            edge.sequence = (int) child.getProperty ("sequence", -1);
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
                << ":" << juce::String (node.position.x, 3) << ":" << juce::String (node.position.y, 3) << ":" << (node.locked ? 1 : 0)
                << ":" << node.routingMode << ":" << node.latencySamples << ":" << (node.solo ? 1 : 0);
            for (const auto& port : node.ports)
                out << "/p:" << port.id << ":" << (int) port.type << ":" << (int) port.direction
                    << ":" << (port.acceptsMultiple ? 1 : 0) << ":" << (port.modulationSafe ? 1 : 0);
        }
        for (const auto& edge : edges)
            out << "|e:" << edge.id << ":" << edge.fromNode << ":" << edge.fromPort << ":"
                << edge.toNode << ":" << edge.toPort << ":" << (int) edge.type << ":" << (edge.editable ? 1 : 0)
                << ":" << edge.sequence;
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
