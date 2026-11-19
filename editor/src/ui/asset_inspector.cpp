#include "crimson_editor/ui/asset_inspector.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include <crimson/asset_manager/importers/texture_importer.hpp>

#include "crimson_editor/ui/asset_field.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/property_fields.hpp"

namespace crimson::editor::ui
{
    // A section whose content is disabled, as a read-only asset's is, while the section itself can still be collapsed
    template <typename Draw>
    static void Section(const char* label, bool disabled, Draw&& draw)
    {
        if (!theme::BeginSection(label))
            return;

        ImGui::BeginDisabled(disabled);
        draw();
        ImGui::EndDisabled();

        theme::EndSection();
    }

    // Whether a texture slot's name says it holds values rather than colors
    static bool HoldsData(const std::string& slot)
    {
        constexpr std::array kDataWords = { "Normal", "Metallic", "Roughness", "Occlusion", "Mask", "Height" };
        return std::ranges::any_of(kDataWords, [&](const char* word) { return slot.find(word) != std::string::npos; });
    }

    template <typename T>
    static std::vector<std::pair<std::string, T>> SortedByName(const std::unordered_map<std::string, T>& map)
    {
        std::vector<std::pair<std::string, T>> items(map.begin(), map.end());
        std::ranges::sort(items, {}, [](const auto& item) { return item.first; });
        return items;
    }

    static std::string FormatFileSize(std::uintmax_t bytes)
    {
        if (bytes < 1024)
            return std::to_string(bytes) + " B";

        if (bytes < 1024 * 1024)
            return std::to_string(bytes / 1024) + " KB";

        return std::to_string(bytes / (1024 * 1024)) + " MB";
    }

    bool PruneToShader(MaterialData& data, const ShaderInterface& shader)
    {
        const std::size_t before = data.Properties.size() + data.Textures.size();

        std::erase_if(data.Properties, [&](const auto& item) { return !shader.Properties.contains(item.first); });
        std::erase_if(data.Textures, [&](const auto& item) { return !shader.Textures.contains(item.first); });

        return data.Properties.size() + data.Textures.size() != before;
    }

    // Prunes for the material's shader, if it loads
    static bool PruneToShader(AssetManager& assets, MaterialData& data)
    {
        const ShaderHandle handle = assets.Load(data.Shader);
        return handle && PruneToShader(data, assets.GetShaderInterface(handle));
    }

    void AssetInspector::Draw(AssetID id)
    {
        const AssetInfo* info = m_context.Assets.GetInfo(id);
        if (!info)
        {
            ImGui::TextDisabled("Nothing selected");
            return;
        }

        DrawHeader(*info);

        switch (info->Type)
        {
            case AssetType::Material: DrawMaterial(*info); break;
            case AssetType::Texture:  DrawTexture(*info); break;
            case AssetType::Mesh:     DrawMesh(*info); break;
            case AssetType::Shader:   DrawShader(*info); break;
            case AssetType::Scene:    DrawScene(*info); break;
            default: break;
        }
    }

    void AssetInspector::DrawScene(const AssetInfo& info)
    {
        const SceneID scene = m_context.Assets.As<SceneAsset>(info.Id);
        const bool open = scene == m_context.ActiveSceneAsset;

        Section("Scene", false, [&]
        {
            ImGui::BeginDisabled(open);
            if (ImGui::Button(open ? "Open Already" : "Open", ImVec2(-1.0f, 0.0f)))
                m_context.OpenScene(scene);
            ImGui::EndDisabled();

            ImGui::TextDisabled("Drag it into the Scene view to use it in another scene");
        });
    }

    void AssetInspector::DrawHeader(const AssetInfo& info)
    {
        const std::string name = std::filesystem::path(info.Path).filename().string();

        // The asset's icon in its type's color, as the Assets panel shows it, then its name
        ImGui::TextColored(theme::AssetColor(info.Type), "%s", icons::ForAsset(info.Type));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
        {
            const theme::ScopedBold bold;
            ImGui::TextUnformatted(m_context.Edits.IsDirty(info.Id) ? (name + " *").c_str() : name.c_str());
        }

        // What it is and where, in a smaller muted line
        {
            const theme::ScopedFont small(nullptr, theme::FontSmall);
            const std::string_view type = ToString(info.Type);
            const std::string where = info.ReadOnly ? "engine, read-only" : info.Path;

            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
            ImGui::TextWrapped("%.*s  \xC2\xB7  %s", static_cast<int>(type.size()), type.data(), where.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
    }

    void AssetInspector::DrawMaterial(const AssetInfo& info)
    {
        AssetEdits& edits = m_context.Edits;
        const MaterialID id = m_context.Assets.As<MaterialAsset>(info.Id);

        const MaterialData* current = edits.Get(id);
        if (!current)
        {
            ImGui::TextDisabled("This material can't be read, see the console");
            return;
        }

        MaterialData data = *current;
        bool changed = false;

        DrawMaterialSettings(id, info.ReadOnly, data, changed);

        const ShaderHandle shader = m_context.Assets.Load(data.Shader);

        if (shader)
        {
            const ShaderInterface& shaderInterface = m_context.Assets.GetShaderInterface(shader);

            if (!shaderInterface.Properties.empty())
                Section("Properties", info.ReadOnly, [&] { DrawMaterialProperties(shaderInterface, data, changed); });

            if (!shaderInterface.Textures.empty())
                Section("Textures", info.ReadOnly, [&] { DrawMaterialTextures(shaderInterface, data, changed); });
        }
        else
        {
            ImGui::TextDisabled("The shader can't be used, see the console");
        }

        if (changed)
        {
            edits.Set(id, data);
            m_context.Edited = true; // a drop or a choice from a list is a step right away, a drag only when it is let go
        }
    }

    void AssetInspector::DrawMaterialSettings(MaterialID id, bool readOnly, MaterialData& data, bool& changed)
    {
        // Another material is drawn, so the picker was left by selecting it and the first one is still unpruned
        if (m_pickingShaderFor && m_pickingShaderFor != id)
        {
            if (const MaterialData* left = m_context.Edits.Get(m_pickingShaderFor))
            {
                MaterialData pruned = *left;
                if (PruneToShader(m_context.Assets, pruned))
                    m_context.Edits.Set(m_pickingShaderFor, pruned);
            }

            m_pickingShaderFor = {};
        }

        Section("Material", readOnly, [&] { DrawMaterialSettingsFields(data, changed); });

        // The picker closed, by Enter or a click outside, or by Escape and the old shader is back. Checked outside the
        // section, so closing it while picking doesn't leave the material unpruned.
        const bool picking = m_shaderField && m_context.Picker.IsOpenFor(m_shaderField);
        if (m_pickingShaderFor == id && !picking && PruneToShader(m_context.Assets, data))
            changed = true;

        m_pickingShaderFor = picking ? id : MaterialID{};
    }

    void AssetInspector::DrawMaterialSettingsFields(MaterialData& data, bool& changed)
    {
        if (!props::BeginTable("##MaterialSettings"))
            return;

        props::BeginRow("Shader");

        ShaderID picked = data.Shader;
        m_shaderField = ImGui::GetID("##Shader");

        if (AssetField<ShaderAsset>("##Shader", m_context, picked))
        {
            data.Shader = picked;
            changed = true;

            // A drop has no picker to wait for
            if (!m_context.Picker.IsOpenFor(m_shaderField))
                PruneToShader(m_context.Assets, data);
        }

        props::EndRow(false);

        static const MaterialSettings kDefaults;

        constexpr std::array kRenderModes = { "Opaque", "Transparent", "Cutout" };
        const int defaultRenderMode = static_cast<int>(kDefaults.RenderMode);
        int renderMode = static_cast<int>(data.Settings.RenderMode);

        if (props::Combo("Render Mode", renderMode, kRenderModes.data(), static_cast<int>(kRenderModes.size()), &defaultRenderMode))
        {
            data.Settings.RenderMode = static_cast<MaterialRenderMode>(renderMode);
            changed = true;
        }

        constexpr std::array kCullModes = { "None", "Back", "Front" };
        const int defaultCull = static_cast<int>(kDefaults.Cull);
        int cull = static_cast<int>(data.Settings.Cull);

        if (props::Combo("Cull", cull, kCullModes.data(), static_cast<int>(kCullModes.size()), &defaultCull))
        {
            data.Settings.Cull = static_cast<CullMode>(cull);
            changed = true;
        }

        props::EndTable();
    }

    void AssetInspector::DrawMaterialProperties(const ShaderInterface& shader, MaterialData& data, bool& changed)
    {
        // In the order the shader declares them
        std::vector<std::pair<std::string, ShaderPropertyInfo>> properties(shader.Properties.begin(), shader.Properties.end());
        std::ranges::sort(properties, {}, [](const auto& item) { return item.second.Offset; });

        if (!props::BeginTable("##MaterialProperties"))
            return;

        for (const auto& [name, property] : properties)
        {
            const std::size_t count = property.Size / sizeof(float);
            if (count < 1 || count > 4)
                continue;

            // What the shader starts the property at, which a reset goes back to
            std::array<float, 4> defaults{};
            std::copy_n(property.Default.begin(), std::min(count, property.Default.size()), defaults.begin());

            std::array<float, 4> values = defaults;
            if (const auto it = data.Properties.find(name); it != data.Properties.end())
                std::copy_n(it->second.begin(), std::min(count, it->second.size()), values.begin());

            const bool isColor = name.find("Color") != std::string::npos;
            const int channels = static_cast<int>(count);
            bool edited = false;

            if (count == 1)
            {
                edited = property.Range
                    ? props::Slider(name.c_str(), values[0], property.Range->first, property.Range->second, &defaults[0])
                    : props::Float(name.c_str(), values[0], 0.01f, 0.0f, 0.0f, &defaults[0]);
            }
            else if (isColor && count >= 3)
            {
                edited = props::Color(name.c_str(), values.data(), channels, defaults.data());
            }
            else
            {
                // A range clamps every component to it
                const auto [min, max] = property.Range.value_or(std::pair{ 0.0f, 0.0f });
                edited = props::Floats(name.c_str(), values.data(), channels, 0.01f, min, max, defaults.data());
            }

            if (!edited)
                continue;

            // A value back at the shader's default isn't the material's own anymore, so the file doesn't keep it
            if (std::equal(values.begin(), values.begin() + channels, defaults.begin()))
                data.Properties.erase(name);
            else
                data.Properties[name] = std::vector<float>(values.begin(), values.begin() + channels);

            changed = true;
        }

        props::EndTable();
    }

    void AssetInspector::DrawMaterialTextures(const ShaderInterface& shader, MaterialData& data, bool& changed)
    {
        if (!props::BeginTable("##MaterialTextures"))
            return;

        const AssetManager& assets = m_context.Assets;

        for (const auto& [name, texture] : SortedByName(shader.Textures))
        {
            props::BeginRow(name.c_str());

            const auto used = data.Textures.find(name);
            const TextureID current = used != data.Textures.end() ? used->second : TextureID{};
            const AssetInfo* currentInfo = assets.GetInfo(current);
            TextureID picked = current;

            ImGui::SetNextItemWidth(-FLT_MIN);
            AssetField<TextureAsset>("##Texture", m_context, picked);

            props::ResetTarget();

            if (currentInfo && HoldsData(name) && currentInfo->Settings.value("srgb", true))
                ImGui::TextColored(theme::Warning, "This holds data, turn sRGB off on the texture");

            // Reset leaves the slot empty, for the shader's own default texture
            if (props::EndRow(static_cast<bool>(current)))
                picked = {};

            if (picked != current)
            {
                if (picked)
                    data.Textures[name] = picked;
                else
                    data.Textures.erase(name);

                changed = true;
            }
        }

        props::EndTable();
    }

    void AssetInspector::ShowTexture(const AssetInfo& info)
    {
        m_shownTexture = info.Id;
        m_textureSettings = info.Settings;
        m_textureSize.reset();
        m_textureFileSize = 0;

        const std::filesystem::path path = m_context.Assets.GetFilePath(info.Id);

        std::ifstream file(path, std::ios::binary);
        if (!file)
            return;

        const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        m_textureFileSize = contents.size();
        m_textureSize = ReadImageSize(std::as_bytes(std::span(contents)));
    }

    void AssetInspector::DrawTexture(const AssetInfo& info)
    {
        if (m_shownTexture != info.Id)
            ShowTexture(info);

        const float available = ImGui::GetContentRegionAvail().x;
        const float aspect = m_textureSize && m_textureSize->second > 0
            ? static_cast<float>(m_textureSize->first) / static_cast<float>(m_textureSize->second)
            : 1.0f;

        if (const ImTextureID preview = m_context.Thumbnails.Get(m_context.Assets.As<TextureAsset>(info.Id)))
            ImGui::Image(preview, ImVec2(available, available / aspect));
        else
            ImGui::TextDisabled("This image can't be read, see the console");

        ImGui::Spacing();

        Section("Details", false, [&]
        {
            if (props::BeginTable("##TextureDetails"))
            {
                if (m_textureSize)
                    props::Text("Size", (std::to_string(m_textureSize->first) + " x " + std::to_string(m_textureSize->second)).c_str());

                props::Text("File", FormatFileSize(m_textureFileSize).c_str());
                props::EndTable();
            }
        });

        Section("Import Settings", false, [&] { DrawImportSettings(info); });
    }

    void AssetInspector::DrawImportSettings(const AssetInfo& info)
    {
        ImGui::BeginDisabled(info.ReadOnly);

        // What a texture is imported with when it has no settings of its own
        static const nlohmann::json kDefaults = TextureImporter().DefaultSettings();

        const auto setting = [this](const char* label, const char* key, const char* tooltip)
        {
            const bool reset = kDefaults.value(key, false);
            bool value = m_textureSettings.value(key, reset);

            if (props::Checkbox(label, value, &reset))
                m_textureSettings[key] = value;

            if (tooltip)
                ImGui::SetItemTooltip("%s", tooltip);
        };

        if (props::BeginTable("##ImportSettings"))
        {
            setting("sRGB", "srgb", "On for colors. Off for data, like normal, roughness and occlusion maps.");
            setting("Generate Mips", "generateMips", nullptr);
            setting("Flip Y", "flipY", nullptr);
            props::EndTable();
        }

        ImGui::EndDisabled();

        if (info.ReadOnly)
            return;

        ImGui::Spacing();
        ImGui::Separator();

        const bool changed = m_textureSettings != info.Settings;
        ImGui::BeginDisabled(!changed);

        if (theme::PrimaryButton("Apply"))
        {
            m_context.Deferred.emplace_back([this, id = m_context.Assets.As<TextureAsset>(info.Id), settings = m_textureSettings]
            {
                // Its id is about to stop working, and the next draw gets a new one
                m_context.Thumbnails.Release(id);
                m_context.Assets.Reimport(id, settings);
            });
        }

        ImGui::SameLine();
        if (ImGui::Button("Revert"))
            m_textureSettings = info.Settings;

        ImGui::EndDisabled();
    }

    void AssetInspector::DrawMesh(const AssetInfo& info)
    {
        const MeshID id = m_context.Assets.As<MeshAsset>(info.Id);

        if (!m_context.Assets.IsLoaded(id))
        {
            // It can take seconds, so only when asked
            if (ImGui::Button("Load to see details"))
                m_context.Deferred.emplace_back([this, id] { m_context.Assets.Load(id); });

            return;
        }

        const Mesh* mesh = m_context.Assets.Load(id);
        if (!mesh)
        {
            ImGui::TextDisabled("This mesh can't be read, see the console");
            return;
        }

        uint64_t indices = 0;
        for (const SubMesh& subMesh : mesh->SubMeshes)
            indices += subMesh.IndexCount;

        Section("Details", false, [&]
        {
            if (!props::BeginTable("##MeshDetails"))
                return;

            const auto vector = [](const glm::vec3& v)
            {
                std::array<char, 64> text{};
                std::snprintf(text.data(), text.size(), "%.2f, %.2f, %.2f", v.x, v.y, v.z);
                return std::string(text.data());
            };

            props::Text("Triangles", std::to_string(indices / 3).c_str());
            props::Text("Sub-meshes", std::to_string(mesh->SubMeshes.size()).c_str());
            props::Text("Bounds Min", vector(mesh->Bounds.Min).c_str());
            props::Text("Bounds Max", vector(mesh->Bounds.Max).c_str());
            props::EndTable();
        });

        Section("Material Slots", false, [&]
        {
            for (const std::string& slot : mesh->MaterialSlots)
                ImGui::BulletText("%s", slot.c_str());
        });
    }

    void AssetInspector::DrawShader(const AssetInfo& info)
    {
        const ShaderHandle shader = m_context.Assets.Load(m_context.Assets.As<ShaderAsset>(info.Id));
        if (!shader)
        {
            ImGui::TextDisabled("This shader doesn't compile, see the console");
            return;
        }

        const ShaderInterface& shaderInterface = m_context.Assets.GetShaderInterface(shader);

        if (!shaderInterface.Properties.empty())
        {
            Section("Properties", false, [&]
            {
                for (const auto& [name, property] : SortedByName(shaderInterface.Properties))
                    ImGui::BulletText("%s (%zu bytes)", name.c_str(), property.Size);
            });
        }

        if (!shaderInterface.Textures.empty())
        {
            Section("Textures", false, [&]
            {
                for (const auto& [name, texture] : SortedByName(shaderInterface.Textures))
                    ImGui::BulletText("%s", name.c_str());
            });
        }
    }
}
