#pragma once
#include <memory>
#include <unordered_map>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/asset_type_registry.hpp"

namespace crimson::editor::ui
{
    // The Inspector's view of the selected asset: the header every asset has, then its type's own view
    class AssetInspector
    {
    public:
        explicit AssetInspector(EditorContext& context) : m_context(context) {}

        void Draw(AssetID id);
    private:
        void DrawHeader(const AssetInfo& info);
    private:
        EditorContext& m_context;

        // One for each type that was selected, so what a view keeps, like a texture's unapplied settings, outlives a selection
        std::unordered_map<AssetType, std::unique_ptr<AssetView>> m_views;
    };
}
