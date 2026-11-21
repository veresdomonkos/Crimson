#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <crimson/asset_manager/importers/material_asset.hpp>

#include "crimson_editor/editor_context.hpp"

namespace crimson::editor
{
    // Drops the properties and textures a shader doesn't have, as they would stay in the file forever. Returns true if any went.
    bool PruneToShader(MaterialData& data, const ShaderInterface& shader);

    // The Inspector's view of the selected asset
    class AssetInspector
    {
    public:
        explicit AssetInspector(EditorContext& context) : m_context(context) {}

        void Draw(AssetID id);
    private:
        void DrawHeader(const AssetInfo& info);
        void DrawMaterial(const AssetInfo& info);
        void DrawMaterialSettings(MaterialID id, bool readOnly, MaterialData& data, bool& changed);
        void DrawMaterialSettingsFields(MaterialData& data, bool& changed);
        void DrawMaterialProperties(const ShaderInterface& shader, MaterialData& data, bool& changed);
        void DrawMaterialTextures(const ShaderInterface& shader, MaterialData& data, bool& changed);
        void DrawTexture(const AssetInfo& info);
        void DrawImportSettings(const AssetInfo& info); // a texture's, with Apply and Revert
        void DrawMesh(const AssetInfo& info);
        void DrawShader(const AssetInfo& info);
        void DrawScene(const AssetInfo& info);

        void ShowTexture(const AssetInfo& info);
    private:
        EditorContext& m_context;

        // While the shader's picker is open the material only takes the shader, so Escape can put the old one back with
        // everything it had. What the shader picked last lacks is dropped once the picker closes.
        MaterialID m_pickingShaderFor; // the material whose shader picker was open
        ui::WidgetId m_shaderField = 0;     // the shader field's id when it was last drawn, which the picker is open for

        // What the selected texture's controls show, until Apply stores them
        AssetID m_shownTexture;
        nlohmann::json m_textureSettings;
        std::optional<std::pair<int, int>> m_textureSize;
        std::uintmax_t m_textureFileSize = 0;
    };
}
