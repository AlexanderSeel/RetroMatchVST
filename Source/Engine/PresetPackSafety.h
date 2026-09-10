#pragma once

#include <JuceHeader.h>

namespace PresetPackSafety
{
inline constexpr int64 maxAssetBytes = 64ll * 1024ll * 1024ll;

inline bool safeRelativePath (juce::String path) noexcept
{
    path = path.replaceCharacter ('\\', '/');
    if (path.isEmpty() || path.startsWithChar ('/') || path.containsChar (':')) return false;

    juce::StringArray parts;
    parts.addTokens (path, "/", "");
    for (const auto& part : parts)
        if (part.isEmpty() || part == "." || part == "..") return false;
    for (const auto character : path)
        if (character < 0x20 || character == 0x7f) return false;
    return true;
}

inline bool safeAssetSize (int64 bytes) noexcept
{
    return bytes >= 0 && bytes <= maxAssetBytes;
}

inline bool validateManifest (const juce::var& manifest, juce::String* reason = nullptr) noexcept
{
    auto fail = [reason] (const juce::String& message)
    {
        if (reason != nullptr) *reason = message;
        return false;
    };
    if (! manifest.isObject()) return fail ("Pack manifest must be an object");
    if (manifest["packId"].toString().trim().isEmpty()) return fail ("Pack manifest is missing packId");
    if (manifest["version"].toString().trim().isEmpty()) return fail ("Pack manifest is missing version");

    const auto assets = manifest["assets"];
    if (! assets.isArray()) return fail ("Pack manifest assets must be an array");
    for (const auto& entry : *assets.getArray())
    {
        const auto path = entry.isString() ? entry.toString() : entry["path"].toString();
        if (! safeRelativePath (path)) return fail ("Pack manifest contains an unsafe asset path");
        if (! entry.isString())
        {
            const auto size = entry["size"];
            if (! size.isInt() && ! size.isInt64() && ! size.isDouble())
                return fail ("Pack manifest asset is missing a numeric size");
            if (! safeAssetSize ((int64) size)) return fail ("Pack manifest asset exceeds the size limit");
        }
    }
    return true;
}

inline bool resolveInside (const juce::File& libraryRoot, const juce::String& relativePath,
                           juce::File& resolved) noexcept
{
    if (! libraryRoot.isDirectory() || ! safeRelativePath (relativePath)) return false;
    const auto root = libraryRoot.getCanonicalFile();
    resolved = root.getChildFile (relativePath).getCanonicalFile();
    return resolved == root || resolved.isAChildOf (root);
}
}
