#include <JuceHeader.h>
#include "../Source/Engine/PresetLibrary.h"
#include "../Source/Matching/GeneratedRackGainPolicy.h"

#include <cmath>
#include <iostream>
#include <set>
#include <string>

namespace
{
int fail (const char* message)
{
    std::cerr << "Preset library test failed: " << message << '\n';
    return 1;
}

bool finite (float value) { return std::isfinite (value); }
}

int main()
{
    {
        constexpr int expectedDepths[] { 3, 4, 6, 8 };
        for (int complexity = 0; complexity < 4; ++complexity)
            if (GeneratedRackGainPolicy::totalInstancesForComplexity (complexity) != expectedDepths[complexity]
                || ! GeneratedRackGainPolicy::isSupportedTotalInstances (expectedDepths[complexity]))
                return fail ("GOLD complexity did not map deterministically to a supported rack depth");
        for (const int unsupported : { 0, 1, 2, 5, 7, 9 })
            if (GeneratedRackGainPolicy::isSupportedTotalInstances (unsupported))
                return fail ("unsupported GOLD rack depth passed the depth gate");
    }

    constexpr int expectedCatalogSize = 10 + FactoryPresetDesign::familyCount * FactoryPresetDesign::variationsPerFamily;
    if ((int) factoryPresetCatalog.size() != expectedCatalogSize || expectedCatalogSize < 300)
        return fail ("factory catalog does not contain the expected 300-plus presets");

    std::set<std::string> names;
    for (int index = 0; index < (int) factoryPresetCatalog.size(); ++index)
    {
        const auto& info = factoryPresetCatalog[(size_t) index];
        if (info.name.isEmpty() || info.category.isEmpty() || info.description.isEmpty())
            return fail ("preset metadata contains an empty required field");
        if (info.author.isEmpty() || info.tags.isEmpty() || info.macroLabels.size() < 2
            || info.recommendedOctaveMin < 1 || info.recommendedOctaveMax > 8
            || info.recommendedOctaveMin > info.recommendedOctaveMax)
            return fail ("factory preset metadata is incomplete or out of bounds");
        if (! names.insert (info.name.toStdString()).second)
            return fail ("factory preset names are not unique");

        const auto p = makeFactoryPreset (index);
        if (! finite (p.cutoff) || ! finite (p.resonance) || ! finite (p.outputGainDb)
            || ! finite (p.attack) || ! finite (p.decay) || ! finite (p.sustain) || ! finite (p.release))
            return fail ("factory preset produced non-finite synthesis parameters");

        int layers = 0;
        float coherent = p.mainLayerGain;
        for (size_t layer = 0; layer < p.layers.size(); ++layer)
        {
            if (! p.layers[layer]) continue;
            ++layers;
            if (p.layerOperation[layer] == 0)
                coherent += p.layerGain[layer] * p.layerAmount[layer];
        }
        if (layers > VoiceParameters::extraLayerCount)
            return fail ("factory preset exceeded the supported layer count");

        if (index >= 10 && coherent > FactoryPresetDesign::coherentLayerBudget + 1.0e-5f)
            return fail ("generated factory preset exceeded its coherent layer budget");
    }

    for (int family = 0; family < FactoryPresetDesign::familyCount; ++family)
    {
        const int begin = 10 + family * FactoryPresetDesign::variationsPerFamily;
        std::set<int> observedDepths;
        for (int variation = 0; variation < FactoryPresetDesign::variationsPerFamily; ++variation)
        {
            const auto p = makeFactoryPreset (begin + variation);
            int layers = 0;
            for (const auto& layer : p.layers) if (layer) ++layers;
            observedDepths.insert (layers + 1);
        }
        if (observedDepths.size() < 5)
            return fail ("a generated family does not provide meaningful rack-depth variation");
    }

    std::cout << "Factory preset library tests passed for " << factoryPresetCatalog.size() << " presets.\n";
    return 0;
}
