#include "crimson_editor/ui/drag_drop.hpp"

#include <string>

#include <imgui.h>

#include "imgui_convert.hpp"

namespace crimson::editor::ui::drag_drop
{
    namespace detail
    {
        void Source(std::string_view type, const void* data, std::size_t size, std::string_view label)
        {
            if (!ImGui::BeginDragDropSource())
                return;

            ImGui::SetDragDropPayload(std::string(type).c_str(), data, size);
            ImGui::TextUnformatted(label.data(), label.data() + label.size());
            ImGui::EndDragDropSource();
        }

        const void* Accept(std::string_view type, std::size_t size, DropLook look, const std::function<bool(const void*)>& accepts)
        {
            if (!ImGui::BeginDragDropTarget())
                return nullptr;

            const std::string name(type);
            const void* dropped = nullptr;

            if (const ImGuiPayload* payload = ImGui::GetDragDropPayload(); payload && payload->IsDataType(name.c_str()) && payload->DataSize == static_cast<int>(size))
            {
                const ImGuiDragDropFlags flags = look == DropLook::None ? ImGuiDragDropFlags_AcceptNoDrawDefaultRect : ImGuiDragDropFlags_None;

                if (accepts(payload->Data) && ImGui::AcceptDragDropPayload(name.c_str(), flags))
                    dropped = payload->Data;
            }

            ImGui::EndDragDropTarget();
            return dropped;
        }

        const void* Peek(std::string_view type, std::size_t size)
        {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (!payload || !payload->IsDataType(std::string(type).c_str()) || payload->DataSize != static_cast<int>(size))
                return nullptr;

            return payload->Data;
        }
    }

    void Highlight(const Rect& rect, float rounding)
    {
        if (!ImGui::IsMouseHoveringRect(ToImVec2(rect.Min), ToImVec2(rect.Max)))
            return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(ToImVec2(rect.Min), ToImVec2(rect.Max), ImGui::GetColorU32(ImGuiCol_DragDropTargetBg), rounding);
        drawList->AddRect(ToImVec2(rect.Min), ToImVec2(rect.Max), ImGui::GetColorU32(ImGuiCol_DragDropTarget), rounding, 0, ImGui::GetStyle().DragDropTargetBorderSize);
    }
}
