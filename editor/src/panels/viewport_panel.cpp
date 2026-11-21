// viewport_panel.cpp
#include "crimson_editor/panels/viewport_panel.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/painter.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor
{
    void ViewportPanel::Draw()
    {
        const glm::vec2 origin = ui::Cursor();
        const glm::vec2 avail = ui::Available();
        m_contentSize = avail;

        if (avail.x > 1.0f && avail.y > 1.0f)
        {
            ui::Painter painter;
            painter.FillRect(ui::Rect{ origin, origin + avail }, ui::theme::Backdrop);

            const glm::vec2 size = (avail.x / avail.y > m_aspect)
                ? glm::vec2(avail.y * m_aspect, avail.y)
                : glm::vec2(avail.x, avail.x / m_aspect);

            m_imageSize = size;

            const glm::vec2 position = origin + (avail - size) * 0.5f;

            ui::SetCursor(position);
            ui::Image(m_texture, size, true);

            OnImageDrawn(position, size);
        }
    }
}
