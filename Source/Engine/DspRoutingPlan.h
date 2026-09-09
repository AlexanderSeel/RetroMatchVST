#pragma once
#include "PatchGraph.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <set>
#include <vector>

namespace DspRouting
{
static constexpr int maxLayerCount = 7;
static constexpr int maxInstanceCount = maxLayerCount + 1;

struct Plan
{
    std::array<int, maxLayerCount> layerOrder {{ 0, 1, 2, 3, 4, 5, 6 }};
    std::array<bool, maxInstanceCount> parallelFx {};
    bool graphAuthored = false;

    bool validPermutation() const noexcept
    {
        std::array<bool, maxLayerCount> seen {};
        for (const int layer : layerOrder)
        {
            if (layer < 0 || layer >= maxLayerCount || seen[(size_t) layer]) return false;
            seen[(size_t) layer] = true;
        }
        return true;
    }
};

struct CompileResult
{
    Plan plan;
    PatchGraph::ValidationResult validation;
};

inline CompileResult compile (const PatchGraph::Document& graph)
{
    CompileResult result;
    if (graph.nodes.empty() && graph.edges.empty()) return result;

    result.validation = graph.validate();
    if (! result.validation.ok) return result;

    struct OrderedLayer { int layer = 0; int sequence = 0; };
    std::vector<OrderedLayer> ordered;
    ordered.reserve (maxLayerCount);
    std::set<int> seen;

    for (const auto& edge : graph.edges)
    {
        if (edge.type != PatchGraph::PortType::audio || edge.toNode != "GLOBALBUS" || ! edge.id.startsWith ("combine:"))
            continue;

        const auto suffix = edge.id.substring (8);
        if (suffix.isEmpty() || ! suffix.containsOnly ("0123456789"))
        {
            result.validation = PatchGraph::ValidationResult::failure ("Invalid layer-combine edge ID: " + edge.id, edge.id);
            return result;
        }

        const int layer = suffix.getIntValue();
        if (layer < 0 || layer >= maxLayerCount || ! seen.insert (layer).second)
        {
            result.validation = PatchGraph::ValidationResult::failure ("Invalid or duplicate layer-combine route: " + edge.id, edge.id);
            return result;
        }
        ordered.push_back ({ layer, edge.sequence >= 0 ? edge.sequence : layer });
    }

    std::sort (ordered.begin(), ordered.end(), [] (const OrderedLayer& a, const OrderedLayer& b)
    {
        if (a.sequence != b.sequence) return a.sequence < b.sequence;
        return a.layer < b.layer;
    });

    std::array<bool, maxLayerCount> emitted {};
    size_t out = 0;
    for (const auto& item : ordered)
    {
        result.plan.layerOrder[out++] = item.layer;
        emitted[(size_t) item.layer] = true;
    }
    for (int layer = 0; layer < maxLayerCount; ++layer)
        if (! emitted[(size_t) layer]) result.plan.layerOrder[out++] = layer;

    for (int instance = 0; instance < maxInstanceCount; ++instance)
    {
        const int layer = instance - 1;
        const auto nodeId = "L" + juce::String (layer) + ":S4";
        if (const auto* fxNode = graph.findNode (nodeId); fxNode != nullptr)
            result.plan.parallelFx[(size_t) instance] = fxNode->routingMode == 1;
    }

    result.plan.graphAuthored = ! ordered.empty()
                             || std::any_of (result.plan.parallelFx.begin(), result.plan.parallelFx.end(), [] (bool value) { return value; });
    if (! result.plan.validPermutation())
        result.validation = PatchGraph::ValidationResult::failure ("Compiled layer routing is not a valid permutation");
    return result;
}

class AtomicPlan
{
public:
    AtomicPlan() noexcept { publish (Plan {}); }

    void publish (const Plan& plan) noexcept
    {
        const auto safe = plan.validPermutation() ? plan : Plan {};
        packed.store (pack (safe), std::memory_order_release);
    }

    Plan snapshot() const noexcept
    {
        return unpack (packed.load (std::memory_order_acquire));
    }

private:
    std::atomic<std::uint32_t> packed { 0 };

    static std::uint32_t pack (const Plan& plan) noexcept
    {
        std::uint32_t value = plan.graphAuthored ? 0x80000000u : 0u;
        for (int i = 0; i < maxLayerCount; ++i)
            value |= (std::uint32_t) (plan.layerOrder[(size_t) i] & 0x7) << (i * 3);
        for (int i = 0; i < maxInstanceCount; ++i)
            if (plan.parallelFx[(size_t) i]) value |= 1u << (21 + i);
        return value;
    }

    static Plan unpack (std::uint32_t value) noexcept
    {
        Plan plan;
        plan.graphAuthored = (value & 0x80000000u) != 0;
        for (int i = 0; i < maxLayerCount; ++i)
            plan.layerOrder[(size_t) i] = (int) ((value >> (i * 3)) & 0x7u);
        for (int i = 0; i < maxInstanceCount; ++i)
            plan.parallelFx[(size_t) i] = (value & (1u << (21 + i))) != 0;
        return plan.validPermutation() ? plan : Plan {};
    }
};
}
