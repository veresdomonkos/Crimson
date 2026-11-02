#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "crimson_editor/editor_context.hpp"

namespace crimson::editor::ui
{
    // The Inspector's view of the selected asset
    class AssetInspector
    {
    public:
        explicit AssetInspector(EditorContext& context) : m_context(context) {}

        void Draw(AssetID id);
    private:
        void DrawHeader(const AssetInfo& info);
        void DrawMaterial(const AssetInfo& info);
        void DrawMaterialSettings(MaterialData& data, bool& changed);
        void DrawMaterialProperties(const ShaderInterface& shader, MaterialData& data, bool& changed);
        void DrawMaterialTextures(const ShaderInterface& shader, MaterialData& data, bool& changed);
        void DrawTexture(const AssetInfo& info);
        void DrawMesh(const AssetInfo& info);
        void DrawShader(const AssetInfo& info);

        void ShowTexture(const AssetInfo& info);
    private:
        EditorContext& m_context;

        // What the selected texture's controls show, until Apply stores them
        AssetID m_shownTexture;
        nlohmann::json m_textureSettings;
        std::optional<std::pair<int, int>> m_textureSize;
        std::uintmax_t m_textureFileSize = 0;
    };
}
