#include "crimson_editor/ui/asset_inspector.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <crimson/asset_manager/importers/texture_importer.hpp>

#include "crimson_editor/ui/asset_drag.hpp"

namespace crimson::editor::ui
{
    template <typename T>
    static std::vector<std::pair<std::string, T>> SortedByName(const std::unordered_map<std::string, T>& map)
    {
        std::vector<std::pair<std::string, T>> items(map.begin(), map.end());
        std::ranges::sort(items, {}, [](const auto& item) { return item.first; });
        return items;
    }

    static void DrawAssetNameColumn(const char* label, const std::string& value)
    {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
        ImGui::TextUnformatted(value.c_str());
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

        // Fields fill the panel, leaving room for their labels
        ImGui::PushItemWidth(-ImGui::GetFontSize() * 6.0f);

        switch (info->Type)
        {
            case AssetType::Material: DrawMaterial(*info); break;
            case AssetType::Texture:  DrawTexture(*info); break;
            case AssetType::Mesh:     DrawMesh(*info); break;
            case AssetType::Shader:   DrawShader(*info); break;
            default: break;
        }

        ImGui::PopItemWidth();
    }

    void AssetInspector::DrawHeader(const AssetInfo& info)
    {
        const std::string name = std::filesystem::path(info.Path).filename().string();

        ImGui::TextUnformatted(m_context.Edits.IsDirty(info.Id) ? (name + " *").c_str() : name.c_str());
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

        const MaterialData* current = edits.GetMaterial(info.Id);
        if (!current)
        {
            ImGui::TextDisabled("This material can't be read, see the console");
            return;
        }

        MaterialData data = *current;
        bool changed = false;

        ImGui::BeginDisabled(info.ReadOnly);

        DrawMaterialSettings(data, changed);

        const ShaderHandle shader = m_context.Assets.LoadShader(data.Shader);

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
            edits.SetMaterial(info.Id, data);

        if (info.ReadOnly)
            return;

        ImGui::Spacing();
        ImGui::Separator();

        const bool dirty = edits.IsDirty(info.Id);
        ImGui::BeginDisabled(!dirty);

        if (ImGui::Button("Save"))
            edits.Save(info.Id);

        ImGui::SameLine();
        if (ImGui::Button("Revert"))
            edits.Revert(info.Id);

        ImGui::EndDisabled();
    }

    void AssetInspector::DrawMaterialSettings(MaterialData& data, bool& changed)
    {
        const AssetManager& assets = m_context.Assets;

        const AssetInfo* currentShader = assets.GetInfo(data.Shader);

        if (ImGui::BeginCombo("Shader", currentShader ? currentShader->Path.c_str() : "None"))
        {
            for (const AssetInfo* shader : assets.List(AssetType::Shader))
            {
                if (ImGui::Selectable(shader->Path.c_str(), shader->Id == data.Shader) && shader->Id != data.Shader)
                {
                    data.Shader = shader->Id;
                    changed = true;

                    // What the new shader doesn't have would stay in the file forever
                    if (const ShaderHandle handle = m_context.Assets.LoadShader(shader->Id))
                    {
                        const ShaderInterface& shaderInterface = m_context.Assets.GetShaderInterface(handle);
                        std::erase_if(data.Properties, [&](const auto& item) { return !shaderInterface.Properties.contains(item.first); });
                        std::erase_if(data.Textures, [&](const auto& item) { return !shaderInterface.Textures.contains(item.first); });
                    }
                }
            }

            ImGui::EndCombo();
        }

        if (const AssetID dropped = AcceptAssetOnLastItem(assets, AssetType::Shader); dropped && dropped != data.Shader)
        {
            data.Shader = dropped;
            changed = true;
        }

        constexpr std::array kRenderModes = { "Opaque", "Transparent" };
        int renderMode = static_cast<int>(data.Settings.RenderMode);

        if (ImGui::Combo("Render Mode", &renderMode, kRenderModes.data(), static_cast<int>(kRenderModes.size())))
        {
            data.Settings.RenderMode = static_cast<MaterialRenderMode>(renderMode);
            changed = true;
        }

        constexpr std::array kCullModes = { "None", "Back", "Front" };
        int cull = static_cast<int>(data.Settings.Cull);

        if (ImGui::Combo("Cull", &cull, kCullModes.data(), static_cast<int>(kCullModes.size())))
        {
            data.Settings.Cull = static_cast<CullMode>(cull);
            changed = true;
        }
    }

    void AssetInspector::DrawMaterialProperties(const ShaderInterface& shader, MaterialData& data, bool& changed)
    {
        // In the order the shader declares them
        std::vector<std::pair<std::string, ShaderPropertyInfo>> properties(shader.Properties.begin(), shader.Properties.end());
        std::ranges::sort(properties, {}, [](const auto& item) { return item.second.Offset; });

        if (properties.empty())
            return;

        ImGui::SeparatorText("Properties");

        for (const auto& [name, property] : properties)
        {
            const std::size_t count = property.Size / sizeof(float);
            if (count < 1 || count > 4)
                continue;

            std::array<float, 4> values{};

            if (const auto it = data.Properties.find(name); it != data.Properties.end())
                std::copy_n(it->second.begin(), std::min(count, it->second.size()), values.begin());

            const bool isColor = name.find("Color") != std::string::npos;
            bool edited = false;

            switch (count)
            {
                case 1: edited = ImGui::DragFloat(name.c_str(), values.data(), 0.01f); break;
                case 2: edited = ImGui::DragFloat2(name.c_str(), values.data(), 0.01f); break;
                // Only the swatch, with the numbers in its popup, as four fields don't fit a narrow panel
                case 3: edited = isColor ? ImGui::ColorEdit3(name.c_str(), values.data(), ImGuiColorEditFlags_NoInputs) : ImGui::DragFloat3(name.c_str(), values.data(), 0.01f); break;
                case 4: edited = isColor ? ImGui::ColorEdit4(name.c_str(), values.data(), ImGuiColorEditFlags_NoInputs) : ImGui::DragFloat4(name.c_str(), values.data(), 0.01f); break;
                default: break;
            }

            if (edited)
            {
                data.Properties[name] = std::vector<float>(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
                changed = true;
            }
        }
    }

    void AssetInspector::DrawMaterialTextures(const ShaderInterface& shader, MaterialData& data, bool& changed)
    {
        if (shader.Textures.empty())
            return;

        ImGui::SeparatorText("Textures");

        const AssetManager& assets = m_context.Assets;
        const float previewSize = ImGui::GetFrameHeight() * 1.5f;

        for (const auto& [name, texture] : SortedByName(shader.Textures))
        {
            ImGui::PushID(name.c_str());

            const auto used = data.Textures.find(name);
            const AssetID current = used != data.Textures.end() ? used->second : AssetID{};

            if (const ImTextureID preview = current ? m_context.Thumbnails.Get(current) : ImTextureID{ 0 })
                ImGui::Image(preview, ImVec2(previewSize, previewSize));
            else
                ImGui::Dummy(ImVec2(previewSize, previewSize));

            ImGui::SameLine();

            const AssetInfo* currentInfo = assets.GetInfo(current);
            AssetID picked = current;

            if (ImGui::BeginCombo(name.c_str(), currentInfo ? currentInfo->Path.c_str() : "None"))
            {
                if (ImGui::Selectable("None", !current))
                    picked = {};

                for (const AssetInfo* candidate : assets.List(AssetType::Texture))
                {
                    if (ImGui::Selectable(candidate->Path.c_str(), candidate->Id == current))
                        picked = candidate->Id;
                }

                ImGui::EndCombo();
            }

            if (const AssetID dropped = AcceptAssetOnLastItem(assets, AssetType::Texture))
                picked = dropped;

            if (picked != current)
            {
                if (picked)
                    data.Textures[name] = picked;
                else
                    data.Textures.erase(name);

                changed = true;
            }

            ImGui::PopID();
        }
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

        if (const ImTextureID preview = m_context.Thumbnails.Get(info.Id))
            ImGui::Image(preview, ImVec2(available, available / aspect));
        else
            ImGui::TextDisabled("This image can't be read, see the console");

        ImGui::Spacing();

        if (m_textureSize)
            DrawAssetNameColumn("Size", std::to_string(m_textureSize->first) + " x " + std::to_string(m_textureSize->second));

        DrawAssetNameColumn("File", FormatFileSize(m_textureFileSize));

        ImGui::SeparatorText("Import Settings");
        ImGui::BeginDisabled(info.ReadOnly);

        bool srgb = m_textureSettings.value("srgb", true);
        if (ImGui::Checkbox("sRGB", &srgb))
            m_textureSettings["srgb"] = srgb;

        ImGui::SetItemTooltip("On for colors. Off for data, like normal, roughness and occlusion maps.");

        bool generateMips = m_textureSettings.value("generateMips", false);
        if (ImGui::Checkbox("Generate Mips", &generateMips))
            m_textureSettings["generateMips"] = generateMips;

        bool flipY = m_textureSettings.value("flipY", false);
        if (ImGui::Checkbox("Flip Y", &flipY))
            m_textureSettings["flipY"] = flipY;

        ImGui::EndDisabled();

        if (info.ReadOnly)
            return;

        ImGui::Spacing();
        ImGui::Separator();

        const bool changed = m_textureSettings != info.Settings;
        ImGui::BeginDisabled(!changed);

        if (ImGui::Button("Apply"))
        {
            m_context.Deferred.emplace_back([this, id = info.Id, settings = m_textureSettings]
            {
                // Its id is about to stop working, and the next draw gets a new one
                m_context.Thumbnails.Release(id);
                m_context.Assets.ReimportTexture(id, settings);
            });
        }

        ImGui::SameLine();
        if (ImGui::Button("Revert"))
            m_textureSettings = info.Settings;

        ImGui::EndDisabled();
    }

    void AssetInspector::DrawMesh(const AssetInfo& info)
    {
        if (!m_context.Assets.IsLoaded(info.Id))
        {
            // It can take seconds, so only when asked
            if (ImGui::Button("Load to see details"))
                m_context.Deferred.emplace_back([this, id = info.Id] { m_context.Assets.LoadMesh(id); });

            return;
        }

        const Mesh* mesh = m_context.Assets.LoadMesh(info.Id);
        if (!mesh)
        {
            ImGui::TextDisabled("This mesh can't be read, see the console");
            return;
        }

        uint64_t indices = 0;
        for (const SubMesh& subMesh : mesh->SubMeshes)
            indices += subMesh.IndexCount;

        DrawAssetNameColumn("Triangles", std::to_string(indices / 3));
        DrawAssetNameColumn("Sub-meshes", std::to_string(mesh->SubMeshes.size()));

        ImGui::TextDisabled("Bounds");
        ImGui::Text("Min %.2f, %.2f, %.2f", mesh->Bounds.Min.x, mesh->Bounds.Min.y, mesh->Bounds.Min.z);
        ImGui::Text("Max %.2f, %.2f, %.2f", mesh->Bounds.Max.x, mesh->Bounds.Max.y, mesh->Bounds.Max.z);

        ImGui::SeparatorText("Material Slots");

        for (const std::string& slot : mesh->MaterialSlots)
            ImGui::BulletText("%s", slot.c_str());
    }

    void AssetInspector::DrawShader(const AssetInfo& info)
    {
        const ShaderHandle shader = m_context.Assets.LoadShader(info.Id);
        if (!shader)
        {
            ImGui::TextDisabled("This shader doesn't compile, see the console");
            return;
        }

        const ShaderInterface& shaderInterface = m_context.Assets.GetShaderInterface(shader);

        ImGui::SeparatorText("Properties");

        for (const auto& [name, property] : SortedByName(shaderInterface.Properties))
            ImGui::BulletText("%s (%zu bytes)", name.c_str(), property.Size);

        ImGui::SeparatorText("Textures");

        for (const auto& [name, texture] : SortedByName(shaderInterface.Textures))
            ImGui::BulletText("%s", name.c_str());
    }
}
