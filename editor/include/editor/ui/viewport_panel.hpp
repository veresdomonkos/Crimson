#pragma once
#include <string>
#include <imgui.h>

#include "editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    class ViewportPanel : public Panel
    {
    public:
        ViewportPanel(std::string_view name, ImTextureID texture, float aspect)
            : m_name(name), m_texture(texture), m_aspect(aspect) {}

        void OnImGui() override;

        void SetTexture(ImTextureID texture) { m_texture = texture; }
        void SetAspect(float aspect)           { m_aspect = aspect; }

        [[nodiscard]] ImVec2 GetContentSize() const { return m_contentSize; }
        [[nodiscard]] bool   IsHovered() const      { return m_hovered; }
        [[nodiscard]] bool   IsFocused() const      { return m_focused; }

    private:
        std::string   m_name;
        ImTextureID m_texture;
        float         m_aspect;

        ImVec2 m_contentSize{0, 0};
        bool   m_hovered = false;
        bool   m_focused = false;
    };
}