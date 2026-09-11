#include <JuceHeader.h>
#include "../Source/Engine/PresetPackSafety.h"

#include <iostream>

namespace
{
int fail (const char* message)
{
    std::cerr << "Preset pack safety test failed: " << message << '\n';
    return 1;
}
}

int main()
{
    using namespace PresetPackSafety;

    if (safeRelativePath ("../outside.rmsynth") || safeRelativePath ("C:/outside.rmsynth")
        || safeRelativePath ("folder//patch.rmsynth") || safeRelativePath ("/absolute.rmsynth")
        || ! safeRelativePath ("patches/bright-pad.rmsynth"))
        return fail ("relative path validation accepted traversal or rejected a safe path");

    Manifest manifest;
    manifest.packId = "retro-pack.test";
    manifest.version = "1.0.0";
    manifest.author = "RetroMatch tests";
    manifest.description = "Small deterministic manifest fixture";
    manifest.minimumSchema = "3";
    manifest.minimumPluginVersion = "1.0.0";
    manifest.tags.addArray ({ "test", "safe" });
    manifest.categories.add ("keys");
    manifest.patchIds.add ("patch-001");
    manifest.assets.push_back ({ "patches/patch-001.rmsynth", 0, {} });

    juce::String reason;
    if (! validateManifest (manifest.toVar(), &reason))
        return fail ("valid manifest was rejected");

    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("retromatch-pack-safety-tests");
    root.deleteRecursively();
    if (! root.createDirectory().wasOk())
        return fail ("test directory could not be created");

    const auto patchFile = root.getChildFile ("patches").getChildFile ("patch-001.rmsynth");
    if (! patchFile.getParentDirectory().createDirectory().wasOk()
        || ! patchFile.replaceWithText ("deterministic patch fixture\n"))
        return fail ("asset fixture could not be written");

    manifest.assets[0].size = patchFile.getSize();
    manifest.assets[0].sha256 = juce::SHA256 (patchFile).toHexString();
    const auto manifestFile = root.getChildFile ("manifest.json");
    if (! writeManifest (manifestFile, manifest, &reason))
        return fail ("validated manifest could not be written");

    Manifest loaded;
    if (! readManifest (manifestFile, loaded, &reason)
        || loaded.packId != manifest.packId || loaded.patchIds != manifest.patchIds
        || loaded.assets.size() != 1 || loaded.assets[0].sha256 != manifest.assets[0].sha256)
        return fail ("manifest round-trip changed typed metadata or asset integrity data");

    if (! verifyAsset (root, patchFile, loaded.assets[0], &reason))
        return fail ("matching asset failed size/hash verification");
    const auto outsideFile = root.getSiblingFile ("retromatch-pack-safety-outside.rmsynth");
    outsideFile.replaceWithText ("deterministic patch fixture\n");
    if (verifyAsset (root, outsideFile, loaded.assets[0], &reason))
        return fail ("asset outside the library root passed root-aware verification");
    if (! patchFile.appendText ("tamper"))
        return fail ("asset tamper fixture could not be written");
    if (verifyAsset (root, patchFile, loaded.assets[0], &reason))
        return fail ("tampered asset passed manifest verification");
    if (! patchFile.replaceWithText ("deterministic patch fixture\n"))
        return fail ("asset fixture could not be restored after tamper coverage");

    juce::File resolved;
    if (! resolveInside (root, "patches/patch-001.rmsynth", resolved)
        || resolved.getFullPathName() != patchFile.getFullPathName()
        || resolveInside (root, "../escape.rmsynth", resolved))
        return fail ("library-root containment check failed");

    auto duplicate = manifest.toVar();
    duplicate.getDynamicObject()->setProperty ("patchIds", Manifest::stringArrayVar ({ "patch-001", "patch-001" }));
    if (validateManifest (duplicate, nullptr))
        return fail ("duplicate patch IDs passed manifest validation");

    auto oversized = manifest.toVar();
    oversized.getDynamicObject()->setProperty ("description", juce::String::repeatedString ('x', maxMetadataCharacters + 1));
    if (validateManifest (oversized, nullptr))
        return fail ("oversized metadata passed manifest validation");

    const juce::StringArray existingIds { "patch-001", "patch-001-copy-1" };
    if (resolveImportConflict ("new-patch", existingIds, ImportConflictPolicy::ask).action != ImportConflictAction::importNew
        || resolveImportConflict ("patch-001", existingIds, ImportConflictPolicy::ask).action != ImportConflictAction::needsDecision
        || resolveImportConflict ("patch-001", existingIds, ImportConflictPolicy::keep).action != ImportConflictAction::keepExisting
        || resolveImportConflict ("patch-001", existingIds, ImportConflictPolicy::replace).action != ImportConflictAction::replaceExisting)
        return fail ("pack conflict policy did not return the requested deterministic action");
    const auto copied = resolveImportConflict ("patch-001", existingIds, ImportConflictPolicy::importAsCopy);
    if (copied.action != ImportConflictAction::importAsCopy || copied.patchId != "patch-001-copy-2")
        return fail ("import-as-copy did not choose a unique deterministic patch ID");

    const auto archiveFile = root.getChildFile ("fixture.rmpack");
    if (! writePackArchive (archiveFile, root, loaded, &reason) || ! archiveFile.existsAsFile())
        return fail ("validated pack archive could not be written");
    juce::MemoryBlock archiveBytes;
    if (! archiveFile.loadFileAsData (archiveBytes))
        return fail ("pack archive could not be read back");
    if (archiveBytes.getSize() < 22 || static_cast<const char*> (archiveBytes.getData())[0] != 'P'
        || static_cast<const char*> (archiveBytes.getData())[1] != 'K')
        return fail ("pack archive does not have a ZIP signature");
    if (! validatePackArchive (archiveFile, &reason))
        return fail ("valid pack archive failed structural ZIP validation");

    root.deleteRecursively();
    outsideFile.deleteFile();
    std::cout << "Preset pack safety tests passed.\n";
    return 0;
}
