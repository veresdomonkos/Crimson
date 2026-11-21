#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "crimson/renderer/property_id.hpp"
#include "crimson/renderer/resource_handles.hpp"
#include "crimson/renderer/shader_property_info.hpp"

namespace crimson
{
    // One property of a MaterialProperties. A texture has its handle and no size, a value has its Size bytes at Offset of the
    // properties' bytes.
    struct MaterialPropertyEntry
    {
        PropertyId Id;
        TextureHandle Texture;
        uint32_t Offset = 0;
        uint32_t Size = 0;
    };

    // Material properties, values and textures, that one draw gets on top of what its material has, without creating a
    // material for it: that draw samples this texture, or reads this color, and the other draws of the material don't.
    // A property the shader doesn't have is ignored. It differs from DrawOptions::MaterialOverride, which replaces the whole
    // material, shader included.
    class MaterialProperties
    {
    public:
        // The value goes in the material's uniform block, at the property of the shader with this id. Setting an id again
        // replaces what it had.
        template<MaterialProperty T>
        MaterialProperties& Set(PropertyId id, const T& value)
        {
            Remove(id);

            const auto offset = static_cast<uint32_t>(m_bytes.size());
            m_bytes.resize(m_bytes.size() + sizeof(T));
            std::memcpy(m_bytes.data() + offset, &value, sizeof(T));

            m_entries.push_back({ id, TextureHandle{}, offset, static_cast<uint32_t>(sizeof(T)) });
            return *this;
        }

        template<MaterialProperty T>
        MaterialProperties& Set(std::string_view name, const T& value) { return Set(PropertyId(name), value); }

        // An invalid texture is the same as not setting one, the material's own texture is used
        MaterialProperties& SetTexture(PropertyId id, TextureHandle texture)
        {
            Remove(id);

            if (texture)
                m_entries.push_back({ id, texture, 0, 0 });

            return *this;
        }

        MaterialProperties& SetTexture(std::string_view name, TextureHandle texture) { return SetTexture(PropertyId(name), texture); }

        [[nodiscard]] std::span<const MaterialPropertyEntry> Entries() const { return m_entries; }
        [[nodiscard]] std::span<const std::byte> Bytes() const { return m_bytes; }
    private:
        void Remove(PropertyId id)
        {
            const auto found = std::find_if(m_entries.begin(), m_entries.end(), [id](const MaterialPropertyEntry& entry) { return entry.Id == id; });

            if (found == m_entries.end())
                return;

            // The bytes after a value move up, so the entries that point at them follow
            if (found->Size > 0)
            {
                const MaterialPropertyEntry removed = *found;
                m_bytes.erase(m_bytes.begin() + removed.Offset, m_bytes.begin() + removed.Offset + removed.Size);

                for (MaterialPropertyEntry& entry : m_entries)
                {
                    if (entry.Size > 0 && entry.Offset > removed.Offset)
                        entry.Offset -= removed.Size;
                }
            }

            m_entries.erase(found);
        }

        std::vector<MaterialPropertyEntry> m_entries;
        std::vector<std::byte> m_bytes;
    };
}
