#include "crimson_editor/ui/asset_type_registry.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <crimson/asset_manager/importers/texture_importer.hpp>

#include "crimson_editor/asset_thumbnails.hpp"
#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/system_shell.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/property_fields.hpp"

namespace crimson::editor::ui
{
    namespace
    {
        std::string FormatFileSize(std::uintmax_t bytes)
        {
            if (bytes < 1024)
                return std::to_string(bytes) + " B";

            if (bytes < 1024 * 1024)
                return std::to_string(bytes / 1024) + " KB";

            return std::to_string(bytes / (1024 * 1024)) + " MB";
        }

        class TextureView : public AssetView
        {
        public:
            explicit TextureView(EditorContext& context) : m_context(context) {}

            void Draw(const AssetInfo& info) override;
        private:
            void DrawImportSettings(const AssetInfo& info); // with Apply and Revert
            void Show(const AssetInfo& info);

            EditorContext& m_context;

            // What the selected texture's controls show, until Apply stores them
            AssetID m_shownTexture;
            nlohmann::json m_textureSettings;
            std::optional<std::pair<int, int>> m_textureSize;
            std::uintmax_t m_textureFileSize = 0;
        };

        void TextureView::Show(const AssetInfo& info)
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

        void TextureView::Draw(const AssetInfo& info)
        {
            if (m_shownTexture != info.Id)
                Show(info);

            const float available = ImGui::GetContentRegionAvail().x;
            const float aspect = m_textureSize && m_textureSize->second > 0
                ? static_cast<float>(m_textureSize->first) / static_cast<float>(m_textureSize->second)
                : 1.0f;

            if (const ImTextureID preview = m_context.Thumbnails.Get(m_context.Assets.As<AssetType::Texture>(info.Id)))
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

        void TextureView::DrawImportSettings(const AssetInfo& info)
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
    }

    EditorAssetType TextureAssetType()
    {
        return {
            .Type = AssetType::Texture,
            .Name = "Texture",
            .Icon = icons::kTexture,
            .Color = ImVec4(0.40f, 0.62f, 0.48f, 1.00f),
            .Thumbnail = [](const AssetManager& assets, AssetThumbnails& thumbnails, AssetID id)
            {
                return thumbnails.Get(assets.As<AssetType::Texture>(id));
            },
            .MakeView = [](EditorContext& context) { return std::make_unique<TextureView>(context); },
            .OpenFile = [](EditorContext&, const std::filesystem::path& file) { shell::OpenInDefaultApp(file); }
        };
    }
}
