#include "crimson_editor/ui/asset_inspector.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <crimson/asset_manager/importers/texture_importer.hpp>

#include "crimson_editor/ui/asset_drag.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/property_fields.hpp"

namespace crimson::editor::ui
{
    // Whether a texture slot's name says it holds values rather than colors
    static bool HoldsData(const std::string& slot)
    {
        constexpr std::array kDataWords = { "Normal", "Metallic", "Roughness", "Occlusion" };
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
        const SceneID scene = m_context.Assets.As<AssetType::Scene>(info.Id);
        const bool open = scene == m_context.ActiveSceneAsset;

        ImGui::BeginDisabled(open);
        if (ImGui::Button(open ? "Open Already" : "Open", ImVec2(-1.0f, 0.0f)))
            m_context.OpenScene(scene);
        ImGui::EndDisabled();

        ImGui::TextDisabled("Drag it into the Scene view to use it in another scene");
    }

    void AssetInspector::DrawHeader(const AssetInfo& info)
    {
        const std::string name = std::filesystem::path(info.Path).filename().string();

        ImGui::TextUnformatted(m_context.Edits.IsDirty(m_context.Assets.As<AssetType::Material>(info.Id)) ? (name + " *").c_str() : name.c_str());
        ImGui::TextDisabled("%.*s", static_cast<int>(ToString(info.Type).size()), ToString(info.Type).data());

        if (info.ReadOnly)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(engine asset, read-only)");
        }

        ImGui::TextWrapped("%s", info.Path.c_str());
        ImGui::Separator();
    }

    void AssetInspector::DrawMaterial(const AssetInfo& info)
    {
        AssetEdits& edits = m_context.Edits;
        const MaterialID id = m_context.Assets.As<AssetType::Material>(info.Id);

        const MaterialData* current = edits.GetMaterial(id);
        if (!current)
        {
            ImGui::TextDisabled("This material can't be read, see the console");
            return;
        }

        MaterialData data = *current;
        bool changed = false;

        ImGui::BeginDisabled(info.ReadOnly);

        DrawMaterialSettings(data, changed);

        const ShaderHandle shader = m_context.Assets.Load(data.Shader);

        if (shader)
        {
            const ShaderInterface& shaderInterface = m_context.Assets.GetShaderInterface(shader);
            DrawMaterialProperties(shaderInterface, data, changed);
            DrawMaterialTextures(shaderInterface, data, changed);
        }
        else
        {
            ImGui::TextDisabled("The shader can't be used, see the console");
        }

        ImGui::EndDisabled();

        if (changed)
            edits.SetMaterial(id, data);

        if (info.ReadOnly)
            return;

        ImGui::Spacing();
        ImGui::Separator();

        const bool dirty = edits.IsDirty(id);
        ImGui::BeginDisabled(!dirty);

        if (theme::PrimaryButton("Save"))
            edits.Save(id);

        ImGui::SameLine();
        if (ImGui::Button("Revert"))
            edits.Revert(id);

        ImGui::EndDisabled();
    }

    void AssetInspector::DrawMaterialSettings(MaterialData& data, bool& changed)
    {
        if (!props::BeginTable("##MaterialSettings"))
            return;

        const AssetManager& assets = m_context.Assets;

        props::BeginRow("Shader");
        if (BeginAssetCombo("##Shader", assets, data.Shader))
        {
            for (const AssetInfo* shader : assets.List(AssetType::Shader))
            {
                if (IsInternal(*shader))
                    continue;

                if (ImGui::Selectable(shader->Path.c_str(), shader->Id == data.Shader) && shader->Id != data.Shader)
                {
                    data.Shader = assets.As<AssetType::Shader>(shader->Id);
                    changed = true;

                    // What the new shader doesn't have would stay in the file forever
                    if (const ShaderHandle handle = m_context.Assets.Load(data.Shader))
                    {
                        const ShaderInterface& shaderInterface = m_context.Assets.GetShaderInterface(handle);
                        std::erase_if(data.Properties, [&](const auto& item) { return !shaderInterface.Properties.contains(item.first); });
                        std::erase_if(data.Textures, [&](const auto& item) { return !shaderInterface.Textures.contains(item.first); });
                    }
                }
            }

            ImGui::EndCombo();
        }

        if (const ShaderID dropped = AcceptAssetOnLastItem<AssetType::Shader>(assets); dropped && dropped != data.Shader)
        {
            data.Shader = dropped;
            changed = true;
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

        if (properties.empty())
            return;

        theme::SectionTitle("Properties");

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
                edited = props::Floats(name.c_str(), values.data(), channels, 0.01f, defaults.data());
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
        if (shader.Textures.empty())
            return;

        theme::SectionTitle("Textures");

        if (!props::BeginTable("##MaterialTextures"))
            return;

        const AssetManager& assets = m_context.Assets;
        const float previewSize = ImGui::GetFrameHeight();

        for (const auto& [name, texture] : SortedByName(shader.Textures))
        {
            props::BeginRow(name.c_str());

            const auto used = data.Textures.find(name);
            const TextureID current = used != data.Textures.end() ? used->second : TextureID{};

            // Not for a missing one, as the empty thumbnail would be kept after the file is back
            if (const ImTextureID preview = current && !assets.IsMissing(current) ? m_context.Thumbnails.Get(current) : ImTextureID{ 0 })
                ImGui::Image(preview, ImVec2(previewSize, previewSize));
            else
                ImGui::Dummy(ImVec2(previewSize, previewSize));

            props::ResetTarget();
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::SetNextItemWidth(-FLT_MIN);

            const AssetInfo* currentInfo = assets.GetInfo(current);
            TextureID picked = current;

            if (BeginAssetCombo("##Texture", assets, current))
            {
                if (ImGui::Selectable("None", !current))
                    picked = {};

                for (const AssetInfo* candidate : assets.List(AssetType::Texture))
                {
                    if (ImGui::Selectable(candidate->Path.c_str(), candidate->Id == current))
                        picked = assets.As<AssetType::Texture>(candidate->Id);
                }

                ImGui::EndCombo();
            }

            if (const TextureID dropped = AcceptAssetOnLastItem<AssetType::Texture>(assets))
                picked = dropped;

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

        if (const ImTextureID preview = m_context.Thumbnails.Get(m_context.Assets.As<AssetType::Texture>(info.Id)))
            ImGui::Image(preview, ImVec2(available, available / aspect));
        else
            ImGui::TextDisabled("This image can't be read, see the console");

        ImGui::Spacing();

        if (props::BeginTable("##TextureDetails"))
        {
            if (m_textureSize)
                props::Text("Size", (std::to_string(m_textureSize->first) + " x " + std::to_string(m_textureSize->second)).c_str());

            props::Text("File", FormatFileSize(m_textureFileSize).c_str());
            props::EndTable();
        }

        theme::SectionTitle("Import Settings");
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
            m_context.Deferred.emplace_back([this, id = m_context.Assets.As<AssetType::Texture>(info.Id), settings = m_textureSettings]
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
        const MeshID id = m_context.Assets.As<AssetType::Mesh>(info.Id);

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

        if (props::BeginTable("##MeshDetails"))
        {
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
        }

        theme::SectionTitle("Material Slots");

        for (const std::string& slot : mesh->MaterialSlots)
            ImGui::BulletText("%s", slot.c_str());
    }

    void AssetInspector::DrawShader(const AssetInfo& info)
    {
        const ShaderHandle shader = m_context.Assets.Load(m_context.Assets.As<AssetType::Shader>(info.Id));
        if (!shader)
        {
            ImGui::TextDisabled("This shader doesn't compile, see the console");
            return;
        }

        const ShaderInterface& shaderInterface = m_context.Assets.GetShaderInterface(shader);

        theme::SectionTitle("Properties");

        for (const auto& [name, property] : SortedByName(shaderInterface.Properties))
            ImGui::BulletText("%s (%zu bytes)", name.c_str(), property.Size);

        theme::SectionTitle("Textures");

        for (const auto& [name, texture] : SortedByName(shaderInterface.Textures))
            ImGui::BulletText("%s", name.c_str());
    }
}
