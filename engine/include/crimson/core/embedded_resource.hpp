#pragma once
#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>

namespace crimson
{
    // A file bundled into the executable by crimson_embed_resources
    struct EmbeddedFile
    {
        std::string_view Path; // relative to the embedded folder, with forward slashes
        std::span<const std::byte> Data;

        [[nodiscard]] std::string_view Text() const { return { reinterpret_cast<const char*>(Data.data()), Data.size() }; }
    };

    [[nodiscard]] inline const EmbeddedFile* FindEmbeddedFile(std::span<const EmbeddedFile> files, std::string_view path)
    {
        const auto it = std::ranges::find(files, path, &EmbeddedFile::Path);
        return it != files.end() ? &*it : nullptr;
    }
}
