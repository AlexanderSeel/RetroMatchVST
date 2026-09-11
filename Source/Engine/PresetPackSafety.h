#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <vector>

namespace PresetPackSafety
{
inline constexpr int64 maxAssetBytes = 64ll * 1024ll * 1024ll;
inline constexpr int maxAssetCount = 4096;
inline constexpr int maxMetadataCharacters = 256;
inline constexpr int maxTagCount = 64;

enum class ImportConflictPolicy { ask, keep, replace, importAsCopy };
enum class ImportConflictAction { importNew, keepExisting, replaceExisting, importAsCopy, needsDecision };

struct ImportConflictResolution
{
    ImportConflictAction action = ImportConflictAction::needsDecision;
    juce::String patchId;
};

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

inline ImportConflictResolution resolveImportConflict (const juce::String& requestedPatchId,
                                                       const juce::StringArray& existingPatchIds,
                                                       ImportConflictPolicy policy)
{
    ImportConflictResolution result;
    result.patchId = requestedPatchId.trim();
    const bool exists = existingPatchIds.contains (result.patchId);
    if (! exists)
    {
        result.action = ImportConflictAction::importNew;
        return result;
    }

    switch (policy)
    {
        case ImportConflictPolicy::ask:
            result.action = ImportConflictAction::needsDecision;
            break;
        case ImportConflictPolicy::keep:
            result.action = ImportConflictAction::keepExisting;
            break;
        case ImportConflictPolicy::replace:
            result.action = ImportConflictAction::replaceExisting;
            break;
        case ImportConflictPolicy::importAsCopy:
        {
            result.action = ImportConflictAction::importAsCopy;
            for (int copy = 1; copy <= maxTagCount; ++copy)
            {
                const auto candidate = result.patchId + "-copy-" + juce::String (copy);
                if (! existingPatchIds.contains (candidate))
                {
                    result.patchId = candidate;
                    break;
                }
            }
            break;
        }
    }
    return result;
}

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
    const auto root = libraryRoot.getFullPathName().replaceCharacter ('\\', '/').trimCharactersAtEnd ("/");
    resolved = libraryRoot.getChildFile (relativePath);
    const auto candidate = resolved.getFullPathName().replaceCharacter ('\\', '/').trimCharactersAtEnd ("/");
    return candidate == root || candidate.startsWith (root + "/");
}

inline bool verifyAsset (const juce::File& libraryRoot, const juce::File& file,
                         const Asset& asset, juce::String* reason = nullptr)
{
    juce::File resolved;
    if (! resolveInside (libraryRoot, asset.path, resolved)
        || resolved.getFullPathName().compare (file.getFullPathName()) != 0)
    {
        if (reason != nullptr) *reason = "Pack asset is outside the declared library root";
        return false;
    }
    return verifyAsset (file, asset, reason);
}

// Deterministic store-only ZIP writer. Compression is deliberately omitted so
// archive creation remains bounded and independent of optional JUCE zip APIs;
// standard ZIP readers can open the resulting .rmpack file.
inline std::uint32_t archiveCrc32 (const void* data, size_t size) noexcept
{
    auto* bytes = static_cast<const std::uint8_t*> (data);
    std::uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline bool writePackArchive (const juce::File& archive, const juce::File& libraryRoot,
                              const Manifest& manifest, juce::String* reason = nullptr)
{
    if (! archive.getFileExtension().equalsIgnoreCase (".rmpack") || ! libraryRoot.isDirectory())
    {
        if (reason != nullptr) *reason = "Pack archive must use .rmpack and a valid library root";
        return false;
    }
    if (! validateManifest (manifest.toVar(), reason)) return false;

    struct Entry { juce::String path; juce::MemoryBlock data; std::uint32_t crc = 0; std::uint32_t offset = 0; };
    std::vector<Entry> entries;
    entries.reserve (manifest.assets.size() + 1);
    Entry manifestEntry;
    manifestEntry.path = "manifest.json";
    const auto manifestText = juce::JSON::toString (manifest.toVar(), true);
    manifestEntry.data.append (manifestText.toRawUTF8(), manifestText.getNumBytesAsUTF8());
    manifestEntry.crc = archiveCrc32 (manifestEntry.data.getData(), manifestEntry.data.getSize());
    entries.push_back (std::move (manifestEntry));

    std::int64_t totalBytes = (std::int64_t) entries.front().data.getSize();
    for (const auto& asset : manifest.assets)
    {
        juce::File resolved;
        if (! resolveInside (libraryRoot, asset.path, resolved) || ! verifyAsset (libraryRoot, resolved, asset, reason))
            return false;
        Entry entry;
        entry.path = asset.path.replaceCharacter ('\\', '/');
        if (! resolved.loadFileAsData (entry.data) || entry.data.getSize() != (size_t) asset.size)
        {
            if (reason != nullptr) *reason = "Unable to read pack asset";
            return false;
        }
        entry.crc = archiveCrc32 (entry.data.getData(), entry.data.getSize());
        totalBytes += (std::int64_t) entry.data.getSize();
        if (totalBytes > 256ll * 1024ll * 1024ll)
        {
            if (reason != nullptr) *reason = "Pack archive exceeds the total size limit";
            return false;
        }
        entries.push_back (std::move (entry));
    }

    if (! archive.getParentDirectory().createDirectory().wasOk() && ! archive.getParentDirectory().isDirectory())
    {
        if (reason != nullptr) *reason = "Unable to create pack archive directory";
        return false;
    }
    juce::FileOutputStream output (archive);
    if (! output.openedOk())
    {
        if (reason != nullptr) *reason = "Unable to create pack archive";
        return false;
    }
    const auto write16 = [&output] (std::uint16_t value)
    { output.writeByte ((char) (value & 0xffu)); output.writeByte ((char) ((value >> 8) & 0xffu)); };
    const auto write32 = [&output, &write16] (std::uint32_t value)
    { write16 (static_cast<std::uint16_t> (value)); write16 (static_cast<std::uint16_t> (value >> 16)); };
    const auto writeName = [&output] (const juce::String& name)
    { output.write (name.toRawUTF8(), (size_t) name.getNumBytesAsUTF8()); };

    for (auto& entry : entries)
    {
        entry.offset = (std::uint32_t) output.getPosition();
        const auto nameBytes = (std::uint16_t) entry.path.getNumBytesAsUTF8();
        write32 (0x04034b50u); write16 (20); write16 (0); write16 (0); write16 (0); write16 (0);
        write32 (entry.crc); write32 ((std::uint32_t) entry.data.getSize()); write32 ((std::uint32_t) entry.data.getSize());
        write16 (nameBytes); write16 (0); writeName (entry.path);
        output.write (entry.data.getData(), entry.data.getSize());
    }
    const auto centralOffset = (std::uint32_t) output.getPosition();
    for (const auto& entry : entries)
    {
        const auto nameBytes = (std::uint16_t) entry.path.getNumBytesAsUTF8();
        write32 (0x02014b50u); write16 (20); write16 (20); write16 (0); write16 (0); write16 (0); write16 (0);
        write32 (entry.crc); write32 ((std::uint32_t) entry.data.getSize()); write32 ((std::uint32_t) entry.data.getSize());
        write16 (nameBytes); write16 (0); write16 (0); write16 (0); write16 (0); write32 (0); write32 (entry.offset);
        writeName (entry.path);
    }
    const auto centralSize = (std::uint32_t) output.getPosition() - centralOffset;
    write32 (0x06054b50u); write16 (0); write16 (0); write16 ((std::uint16_t) entries.size()); write16 ((std::uint16_t) entries.size());
    write32 (centralSize); write32 (centralOffset); write16 (0);
    output.flush();
    return output.getStatus().wasOk();
}

inline bool validatePackArchive (const juce::File& archive, juce::String* reason = nullptr)
{
    auto fail = [reason] (const juce::String& message)
    {
        if (reason != nullptr) *reason = message;
        return false;
    };
    if (! archive.existsAsFile() || archive.getSize() < 22 || archive.getSize() > 256ll * 1024ll * 1024ll)
        return fail ("Pack archive size is outside the allowed range");

    juce::MemoryBlock bytes;
    if (! archive.loadFileAsData (bytes))
        return fail ("Pack archive could not be read");
    const auto* data = static_cast<const std::uint8_t*> (bytes.getData());
    const size_t size = bytes.getSize();
    const auto read16 = [data, size] (size_t offset) -> std::uint16_t
    { return offset + 2 <= size ? (std::uint16_t) data[offset] | ((std::uint16_t) data[offset + 1] << 8) : 0xffffu; };
    const auto read32 = [data, size] (size_t offset) -> std::uint32_t
    { return offset + 4 <= size ? (std::uint32_t) data[offset] | ((std::uint32_t) data[offset + 1] << 8)
                                      | ((std::uint32_t) data[offset + 2] << 16) | ((std::uint32_t) data[offset + 3] << 24) : 0xffffffffu; };

    size_t endRecord = size;
    bool foundEndRecord = false;
    const size_t searchStart = size > 65557 ? size - 65557 : 0;
    while (endRecord-- > searchStart)
        if (read32 (endRecord) == 0x06054b50u) { foundEndRecord = true; break; }
    if (! foundEndRecord || read32 (endRecord + 20) != 0)
        return fail ("Pack archive has no valid ZIP end record");
    const auto entryCount = read16 (endRecord + 10);
    const auto centralSize = read32 (endRecord + 12);
    const auto centralOffset = read32 (endRecord + 16);
    if (entryCount == 0 || centralOffset > size || centralSize > size - centralOffset
        || centralOffset + centralSize > endRecord)
        return fail ("Pack archive central directory is malformed");

    juce::StringArray paths;
    size_t cursor = centralOffset;
    for (std::uint16_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
    {
        if (cursor + 46 > size || read32 (cursor) != 0x02014b50u)
            return fail ("Pack archive contains an invalid central directory entry");
        const auto method = read16 (cursor + 10);
        const auto compressed = read32 (cursor + 20);
        const auto uncompressed = read32 (cursor + 24);
        const auto nameLength = read16 (cursor + 28);
        const auto extraLength = read16 (cursor + 30);
        const auto commentLength = read16 (cursor + 32);
        const auto localOffset = read32 (cursor + 42);
        const size_t recordSize = 46ull + nameLength + extraLength + commentLength;
        if (method != 0 || compressed != uncompressed || cursor + recordSize > size
            || nameLength == 0 || nameLength > maxMetadataCharacters)
            return fail ("Pack archive entry uses unsupported compression or invalid bounds");
        const auto path = juce::String::fromUTF8 (reinterpret_cast<const char*> (data + cursor + 46), nameLength)
                              .replaceCharacter ('\\', '/');
        if (! safeRelativePath (path) || paths.contains (path))
            return fail ("Pack archive contains an unsafe or duplicate path");
        paths.add (path);
        if ((size_t) localOffset + 30 > size || read32 (localOffset) != 0x04034b50u)
            return fail ("Pack archive entry has an invalid local record");
        const auto localNameLength = read16 (localOffset + 26);
        const auto localExtraLength = read16 (localOffset + 28);
        const size_t payload = (size_t) localOffset + 30ull + localNameLength + localExtraLength;
        if (payload > size || compressed > size - payload || archiveCrc32 (data + payload, compressed) != read32 (cursor + 16))
            return fail ("Pack archive entry payload or CRC is invalid");
        cursor += recordSize;
    }
    return cursor == centralOffset + centralSize ? true : fail ("Pack archive central directory size is inconsistent");
}
}
