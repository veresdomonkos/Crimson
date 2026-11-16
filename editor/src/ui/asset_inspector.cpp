#include "crimson_editor/ui/asset_inspector.hpp"

#include <filesystem>
#include <string>

#include <imgui.h>

#include "crimson_editor/ui/editor_theme.hpp"

namespace crimson::editor::ui
{
    void AssetInspector::Draw(AssetID id)
    {
        const AssetInfo* info = m_context.Assets.GetInfo(id);
        if (!info)
        {
            ImGui::TextDisabled("Nothing selected");
            return;
        }

        DrawHeader(*info);

        const EditorAssetType* kind = m_context.AssetTypes.Find(info->Type);
        if (!kind || !kind->MakeView)
            return;

        std::unique_ptr<AssetView>& view = m_views[info->Type];
        if (!view)
            view = kind->MakeView(m_context);

        view->Draw(*info);
    }

    void AssetInspector::DrawHeader(const AssetInfo& info)
    {
        const std::string name = std::filesystem::path(info.Path).filename().string();

        // The asset's icon in its type's color, as the Assets panel shows it, then its name
        const EditorAssetType& kind = m_context.AssetTypes.Get(info.Type);
        ImGui::TextColored(kind.Color, "%s", kind.Icon);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
        {
            const theme::ScopedBold bold;
            ImGui::TextUnformatted(m_context.Edits.IsDirty(info.Id) ? (name + " *").c_str() : name.c_str());
        }

        // What it is and where, in a smaller muted line
        {
            const theme::ScopedFont small(nullptr, theme::FontSmall);
            const std::string where = info.ReadOnly ? "engine, read-only" : info.Path;

            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
            ImGui::TextWrapped("%s  \xC2\xB7  %s", kind.Name.c_str(), where.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
    }
}
