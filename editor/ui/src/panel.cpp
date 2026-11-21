#include "crimson_editor/ui/panel.hpp"

#include <utility>

#include <imgui.h>

namespace crimson::editor::ui
{
    Panel::Panel(PanelInfo info)
        : m_info(std::move(info)),
          m_open(m_info.OpenAtStart)
    {
    }

    void Panel::Show()
    {
        if (!m_open)
        {
            m_open = true;
            OnOpened();
        }

        // From inside the panel's own window it can be focused at once
        if (m_drawing)
            ImGui::SetWindowFocus();
        else
            m_focusRequested = true;
    }

    void Panel::AddCommand(Command command)
    {
        m_commands.push_back(std::move(command));
    }
}
