#include "crimson_editor/ui/material_view.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/asset_field.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/property_fields.hpp"

namespace crimson::editor::ui
{
    // Whether a texture slot's name says it holds values rather than colors
    static bool HoldsData(const std::string& slot)
    {
        constexpr std::array kDataWords = { "Normal", "Metallic", "Roughness", "Occlusion", "Mask", "Height" };
        return std::ranges::any_of(kDataWords, [&](const char* word) { return slot.find(word) != std::string::npos; });
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

    void MaterialView::Draw(const AssetInfo& info)
    {
        Draw(m_context.Assets.As<AssetType::Material>(info.Id), info.ReadOnly);
    }

    void MaterialView::Draw(MaterialID id, bool readOnly)
    {
        AssetEdits& edits = m_context.Edits;

        const MaterialData* current = edits.Get(id);
        if (!current)
        {
            ImGui::TextDisabled("This material can't be read, see the console");
            return;
        }

        MaterialData data = *current;
        bool changed = false;

        DrawMaterialSettings(id, readOnly, data, changed);

        const ShaderHandle shader = m_context.Assets.Load(data.Shader);

        if (shader)
        {
            const ShaderInterface& shaderInterface = m_context.Assets.GetShaderInterface(shader);

            if (!shaderInterface.Properties.empty())
                Section("Properties", readOnly, [&] { DrawMaterialProperties(shaderInterface, data, changed); });

            if (!shaderInterface.Textures.empty())
                Section("Textures", readOnly, [&] { DrawMaterialTextures(shaderInterface, data, changed); });
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

    void MaterialView::DrawMaterialSettings(MaterialID id, bool readOnly, MaterialData& data, bool& changed)
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

    void MaterialView::DrawMaterialSettingsFields(MaterialData& data, bool& changed)
    {
        if (!props::BeginTable("##MaterialSettings"))
            return;

        props::BeginRow("Shader");

        ShaderID picked = data.Shader;
        m_shaderField = ImGui::GetID("##Shader");

        if (AssetField<AssetType::Shader>("##Shader", m_context, picked))
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

    void MaterialView::DrawMaterialProperties(const ShaderInterface& shader, MaterialData& data, bool& changed)
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

    void MaterialView::DrawMaterialTextures(const ShaderInterface& shader, MaterialData& data, bool& changed)
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
            AssetField<AssetType::Texture>("##Texture", m_context, picked);

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

    EditorAssetType MaterialAssetType()
    {
        return {
            .Type = AssetType::Material,
            .Name = "Material",
            .Icon = icons::kMaterial,
            .Color = ImVec4(0.66f, 0.42f, 0.52f, 1.00f),
            .MakeView = [](EditorContext& context) { return std::make_unique<MaterialView>(context); },
            .Creation = AssetCreation{
                .Label = "New Material",
                .DefaultName = "New Material",
                .Create = [](AssetManager& assets, const std::string& path) { return AssetID(assets.Create<AssetType::Material>(path + ".mat")); }
            }
        };
    }
}
