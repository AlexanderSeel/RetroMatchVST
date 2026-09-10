#pragma once

#include <JuceHeader.h>
#include <vector>

namespace PresetPackSafety
{
inline constexpr int64 maxAssetBytes = 64ll * 1024ll * 1024ll;
inline constexpr int maxAssetCount = 4096;
inline constexpr int maxMetadataCharacters = 256;
inline constexpr int maxTagCount = 64;

inline bool validateManifest (const juce::var&, juce::String* reason) noexcept;

struct Asset
{
    juce::String path;
    int64 size = 0;
    juce::String sha256;
};

struct Manifest
{
    juce::String packId, version, author, description;
    juce::String minimumSchema, minimumPluginVersion;
    juce::StringArray tags, categories, patchIds;
    std::vector<Asset> assets;

    static juce::var stringArrayVar (const juce::StringArray& values)
    {
        juce::Array<juce::var> result;
        for (const auto& value : values) result.add (value);
        return juce::var (result);
    }

    juce::var toVar() const
    {
        auto object = new juce::DynamicObject();
        object->setProperty ("packId", packId);
        object->setProperty ("version", version);
        object->setProperty ("author", author);
        object->setProperty ("description", description);
        object->setProperty ("minimumSchema", minimumSchema);
        object->setProperty ("minimumPluginVersion", minimumPluginVersion);
        object->setProperty ("tags", stringArrayVar (tags));
        object->setProperty ("categories", stringArrayVar (categories));
        object->setProperty ("patchIds", stringArrayVar (patchIds));
        juce::Array<juce::var> assetValues;
        for (const auto& asset : assets)
        {
            auto entry = new juce::DynamicObject();
            entry->setProperty ("path", asset.path);
            entry->setProperty ("size", asset.size);
            if (asset.sha256.isNotEmpty()) entry->setProperty ("sha256", asset.sha256);
            assetValues.add (juce::var (entry));
        }
        object->setProperty ("assets", juce::var (assetValues));
        return juce::var (object);
    }

    static bool fromVar (const juce::var& value, Manifest& result, juce::String* reason = nullptr)
    {
        if (! validateManifest (value, reason)) return false;
        Manifest parsed;
        parsed.packId = value["packId"].toString(); parsed.version = value["version"].toString();
        parsed.author = value["author"].toString(); parsed.description = value["description"].toString();
        parsed.minimumSchema = value["minimumSchema"].toString();
        parsed.minimumPluginVersion = value["minimumPluginVersion"].toString();
        if (value["tags"].isArray())
            for (const auto& tag : *value["tags"].getArray()) parsed.tags.add (tag.toString());
        if (value["categories"].isArray())
            for (const auto& category : *value["categories"].getArray()) parsed.categories.add (category.toString());
        if (value["patchIds"].isArray())
            for (const auto& patchId : *value["patchIds"].getArray()) parsed.patchIds.add (patchId.toString());
        for (const auto& entry : *value["assets"].getArray())
        {
            Asset asset;
            asset.path = entry.isString() ? entry.toString() : entry["path"].toString();
            asset.size = entry.isString() ? 0 : (int64) entry["size"];
            if (! entry.isString()) asset.sha256 = entry["sha256"].toString();
            parsed.assets.push_back (std::move (asset));
        }
        result = std::move (parsed);
        return true;
    }
};

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

inline bool writeManifest (const juce::File& file, const Manifest& manifest, juce::String* reason = nullptr)
{
    if (! file.getFileName().equalsIgnoreCase ("manifest.json"))
    {
        if (reason != nullptr) *reason = "Pack manifest must be named manifest.json";
        return false;
    }
    const auto value = manifest.toVar();
    if (! validateManifest (value, reason)) return false;
    if (! file.getParentDirectory().createDirectory().wasOk() && ! file.getParentDirectory().isDirectory())
    {
        if (reason != nullptr) *reason = "Unable to create manifest directory";
        return false;
    }
    if (! file.replaceWithText (juce::JSON::toString (value, true)))
    {
        if (reason != nullptr) *reason = "Unable to write pack manifest";
        return false;
    }
    return true;
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
    for (const auto* key : { "packId", "version", "author", "description", "minimumSchema", "minimumPluginVersion" })
        if (manifest[key].toString().length() > maxMetadataCharacters)
            return fail ("Pack manifest metadata field is too long");

    const auto assets = manifest["assets"];
    if (! assets.isArray()) return fail ("Pack manifest assets must be an array");
    if (assets.getArray()->size() > maxAssetCount) return fail ("Pack manifest contains too many assets");
    for (const auto* key : { "tags", "categories", "patchIds" })
    {
        const auto values = manifest[key];
        if (! values.isVoid() && ! values.isArray()) return fail ("Pack manifest metadata list must be an array");
        if (values.isArray() && values.getArray()->size() > maxTagCount) return fail ("Pack manifest contains too many metadata IDs or labels");
        if (values.isArray())
        {
            juce::StringArray seen;
            for (const auto& value : *values.getArray())
            {
                const auto text = value.toString();
                if (! value.isString() || text.trim().isEmpty() || text.length() > maxMetadataCharacters
                    || (juce::String (key) == "patchIds" && seen.contains (text)))
                    return fail ("Pack manifest contains an invalid metadata ID or label");
                seen.add (text);
            }
        }
    }
    juce::StringArray paths;
    for (const auto& entry : *assets.getArray())
    {
        const auto path = (entry.isString() ? entry.toString() : entry["path"].toString()).replaceCharacter ('\\', '/');
        if (! safeRelativePath (path)) return fail ("Pack manifest contains an unsafe asset path");
        if (path.length() > maxMetadataCharacters || paths.contains (path))
            return fail ("Pack manifest contains a duplicate or overlong asset path");
        paths.add (path);
        if (! entry.isString())
        {
            const auto size = entry["size"];
            if (! size.isInt() && ! size.isInt64() && ! size.isDouble())
                return fail ("Pack manifest asset is missing a numeric size");
            if (! safeAssetSize ((int64) size)) return fail ("Pack manifest asset exceeds the size limit");
            const auto hash = entry["sha256"].toString();
            if (hash.isNotEmpty() && (hash.length() != 64 || ! hash.containsOnly ("0123456789abcdefABCDEF")))
                return fail ("Pack manifest asset has an invalid SHA-256 hash");
        }
    }
    return true;
}

inline bool readManifest (const juce::File& file, Manifest& result, juce::String* reason = nullptr)
{
    if (! file.getFileName().equalsIgnoreCase ("manifest.json") || ! file.existsAsFile())
    {
        if (reason != nullptr) *reason = "Pack manifest file is missing or has the wrong name";
        return false;
    }
    if (file.getSize() <= 0 || file.getSize() > 1024 * 1024)
    {
        if (reason != nullptr) *reason = "Pack manifest size is outside the allowed range";
        return false;
    }
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    return Manifest::fromVar (parsed, result, reason);
}

inline bool verifyAsset (const juce::File& file, const Asset& asset, juce::String* reason = nullptr)
{
    if (! file.existsAsFile() || ! safeRelativePath (asset.path) || ! safeAssetSize (asset.size))
    {
        if (reason != nullptr) *reason = "Pack asset is missing or has invalid metadata";
        return false;
    }
    if (file.getSize() != asset.size)
    {
        if (reason != nullptr) *reason = "Pack asset size does not match the manifest";
        return false;
    }
    if (asset.sha256.isNotEmpty())
    {
        const auto actual = juce::SHA256 (file).toHexString();
        if (! actual.equalsIgnoreCase (asset.sha256))
        {
            if (reason != nullptr) *reason = "Pack asset SHA-256 does not match the manifest";
            return false;
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
