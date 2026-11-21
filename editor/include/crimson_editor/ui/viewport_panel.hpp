#pragma once
#include <utility>

#include <crimson/renderer/resource_handles.hpp>
#include <imgui.h>

#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    // Shows a texture at its aspect ratio, in a window that is all image
    class ViewportPanel : public Panel
    {
    public:
        ViewportPanel(PanelInfo info, TextureHandle texture, float aspect)
            : Panel(std::move(info)), m_texture(texture), m_aspect(aspect) {}

        void SetTexture(TextureHandle texture) { m_texture = texture; }
        void SetAspect(float aspect)           { m_aspect = aspect; }

        [[nodiscard]] ImVec2 GetContentSize() const { return m_contentSize; }
        [[nodiscard]] ImVec2 GetImageSize() const { return m_imageSize; }

    protected:
        void Draw() override;

        // Called with the image's rect on screen, which leaves out the bars around it
        virtual void OnImageDrawn(const ImVec2& position, const ImVec2& size) {}

    private:
        TextureHandle m_texture;
        float         m_aspect;

        ImVec2 m_contentSize{0, 0};
        ImVec2 m_imageSize{0, 0};
    };
}
