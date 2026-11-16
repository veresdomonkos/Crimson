#pragma once
#include "crimson_editor/ui/asset_type_registry.hpp"

namespace crimson::editor::ui
{
    // Drops the properties and textures a shader doesn't have, as they would stay in the file forever. Returns true if any went.
    bool PruneToShader(MaterialData& data, const ShaderInterface& shader);

    // A material's sections, which can be drawn on their own from an id, as the Inspector does for a selected material
    class MaterialView : public AssetView
    {
    public:
        explicit MaterialView(EditorContext& context) : m_context(context) {}

        // Edits go through the context's AssetEdits. A read-only material's fields are disabled, and its sections still collapse.
        void Draw(MaterialID id, bool readOnly);
        void Draw(const AssetInfo& info) override;
    private:
        void DrawMaterialSettings(MaterialID id, bool readOnly, MaterialData& data, bool& changed);
        void DrawMaterialSettingsFields(MaterialData& data, bool& changed);
        void DrawMaterialProperties(const ShaderInterface& shader, MaterialData& data, bool& changed);
        void DrawMaterialTextures(const ShaderInterface& shader, MaterialData& data, bool& changed);

        EditorContext& m_context;

        // While the shader's picker is open the material only takes the shader, so Escape can put the old one back with
        // everything it had. What the shader picked last lacks is dropped once the picker closes.
        MaterialID m_pickingShaderFor; // the material whose shader picker was open
        ImGuiID m_shaderField = 0;     // the shader field's id when it was last drawn, which the picker is open for
    };
}
