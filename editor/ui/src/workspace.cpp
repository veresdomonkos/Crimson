#include "crimson_editor/ui/workspace.hpp"

#include <algorithm>
#include <span>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include "command_menu.hpp"
#include "imgui_convert.hpp"

namespace crimson::editor::ui
{
    static ImGuiKeyChord ToChord(const Shortcut& shortcut)
    {
        ImGuiKeyChord chord = ToImGuiKey(shortcut.Key);

        if (HasModifier(shortcut.Modifiers, KeyModifiers::Control))
            chord |= ImGuiMod_Ctrl;
        if (HasModifier(shortcut.Modifiers, KeyModifiers::Shift))
            chord |= ImGuiMod_Shift;
        if (HasModifier(shortcut.Modifiers, KeyModifiers::Alt))
            chord |= ImGuiMod_Alt;
        if (HasModifier(shortcut.Modifiers, KeyModifiers::Super))
            chord |= ImGuiMod_Super;

        return chord;
    }

    // Runs the command if its shortcut was pressed. A scoped one is called inside its panel's window, where the route
    // goes to the window while it is focused. A global one is routed to whoever claims the keys first, so a text field
    // that is active keeps the ones it uses, such as Ctrl+Z.
    static void RunIfPressed(const Command& command, bool scoped)
    {
        if (command.Enabled && !command.Enabled())
            return;

        const ImGuiIO& io = ImGui::GetIO();

        for (const Shortcut& shortcut : command.Shortcuts)
        {
            // A key without Ctrl or Alt is a letter in a text field
            if (io.WantTextInput && !HasModifier(shortcut.Modifiers, KeyModifiers::Control | KeyModifiers::Alt))
                continue;

            ImGuiInputFlags flags = scoped ? ImGuiInputFlags_RouteFocused : ImGuiInputFlags_RouteGlobal;
            if (command.Repeats)
                flags |= ImGuiInputFlags_Repeat;

            if (ImGui::Shortcut(ToChord(shortcut), flags))
            {
                command.Run();
                return;
            }
        }
    }

    static void DrawMenuNode(const MenuNode& node, bool inBar)
    {
        if (node.SeparatorBefore && !inBar)
            ImGui::Separator();

        if (node.Item)
        {
            const Command& command = *node.Item;
            const std::string shortcut = command.Shortcuts.empty() ? std::string() : ToText(command.Shortcuts.front());
            const bool enabled = !command.Enabled || command.Enabled();

            if (ImGui::MenuItem(node.Name.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), false, enabled))
                command.Run();

            return;
        }

        if (!ImGui::BeginMenu(node.Name.c_str()))
            return;

        for (const MenuNode& child : node.Children)
            DrawMenuNode(child, false);

        ImGui::EndMenu();
    }

    // The same splits as the layout has always had. Panels are docked in the order they are drawn in.
    static void BuildDefaultLayout(ImGuiID dockSpace, const ImVec2& size, std::span<Panel* const> panels)
    {
        ImGui::DockBuilderRemoveNode(dockSpace);
        ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(dockSpace, size);

        ImGuiID center = dockSpace;
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f, nullptr, &center);
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.35f, nullptr, &right);

        for (const Panel* panel : panels)
        {
            ImGuiID node = 0;

            switch (panel->Info().Slot)
            {
                case DockSlot::Left: node = left; break;
                case DockSlot::Center: node = center; break;
                case DockSlot::Bottom: node = bottom; break;
                case DockSlot::Right: node = right; break;
                case DockSlot::RightBottom: node = rightBottom; break;
                case DockSlot::Floating: break;
            }

            if (node != 0)
                ImGui::DockBuilderDockWindow(panel->Info().Title.c_str(), node);
        }

        ImGui::DockBuilderFinish(dockSpace);
    }

    Workspace::Workspace() = default;
    Workspace::~Workspace() = default;

    void Workspace::Add(std::unique_ptr<Panel> panel)
    {
        m_drawOrder.push_back(panel.get());
        SortForDrawing(m_drawOrder);

        m_panels.push_back(std::move(panel));
    }

    void Workspace::AddCommand(Command command)
    {
        m_commands.push_back(std::move(command));
    }

    void Workspace::Draw()
    {
        ImGuizmo::BeginFrame();

        for (const Command& command : m_commands)
            RunIfPressed(command, false);

        DrawMainMenu();
        DrawDockSpace();

        for (Panel* panel : m_drawOrder)
            DrawPanel(*panel);
    }

    void Workspace::DrawMainMenu()
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        std::vector<const Command*> commands;
        for (const Command& command : m_commands)
            commands.push_back(&command);

        for (const Panel* panel : m_drawOrder)
        {
            if (!panel->IsOpen())
                continue;

            for (const Command& command : panel->Commands())
                commands.push_back(&command);
        }

        for (const MenuNode& menu : BuildMenu(commands).Children)
            DrawMenuNode(menu, true);

        DrawViewMenu();

        ImGui::EndMainMenuBar();
    }

    void Workspace::DrawViewMenu()
    {
        if (!ImGui::BeginMenu("View"))
            return;

        for (Panel* panel : m_drawOrder)
        {
            const PanelInfo& info = panel->Info();
            if (!info.Closable)
                continue;

            const std::string label = info.Icon ? std::string(info.Icon) + "  " + info.Title : info.Title;

            if (ImGui::MenuItem(label.c_str(), nullptr, panel->IsOpen()))
            {
                if (panel->IsOpen())
                    panel->Close();
                else
                    panel->Show();
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reset Layout"))
            m_resetLayout = true;

        ImGui::EndMenu();
    }

    void Workspace::DrawDockSpace()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground;

        const ImGuiID dockSpace = ImGui::GetID("MyDockSpace");

        // A layout saved in imgui.ini already created the node, so this only runs on a first start
        if (m_resetLayout || !ImGui::DockBuilderGetNode(dockSpace))
        {
            BuildDefaultLayout(dockSpace, viewport->WorkSize, m_drawOrder);
            m_resetLayout = false;
        }

        ImGui::Begin("DockSpaceHost", nullptr, flags);
        ImGui::DockSpace(dockSpace, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();
    }

    // A layout saved before a panel existed doesn't know about it, so it joins one that is docked in its slot
    void Workspace::DockWithSiblings(const Panel& panel)
    {
        const PanelInfo& info = panel.Info();
        if (info.Slot == DockSlot::Floating || ImGui::FindWindowSettingsByID(ImHashStr(info.Title.c_str())))
            return;

        for (const Panel* other : m_drawOrder)
        {
            if (other == &panel || other->Info().Slot != info.Slot)
                continue;

            const ImGuiWindow* window = ImGui::FindWindowByName(other->Info().Title.c_str());
            if (window && window->DockId != 0)
            {
                ImGui::SetNextWindowDockID(window->DockId, ImGuiCond_FirstUseEver);
                return;
            }
        }
    }

    void Workspace::DrawPanel(Panel& panel)
    {
        if (!panel.m_open)
        {
            panel.m_focused = false;
            panel.m_hovered = false;
            return;
        }

        const PanelInfo& info = panel.Info();

        if (panel.m_focusRequested)
        {
            ImGui::SetNextWindowFocus();
            panel.m_focusRequested = false;
        }

        DockWithSiblings(panel);

        if (info.DefaultSize.x != 0.0f || info.DefaultSize.y != 0.0f)
        {
            const float fontSize = ImGui::GetFontSize();
            ImGui::SetNextWindowSize(ImVec2(info.DefaultSize.x * fontSize, info.DefaultSize.y * fontSize), ImGuiCond_FirstUseEver);
        }

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (info.FillsWindow)
        {
            flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        }

        bool open = true;
        ImGui::Begin(info.Title.c_str(), info.Closable ? &open : nullptr, flags);

        if (info.FillsWindow)
            ImGui::PopStyleVar();

        panel.m_focused = ImGui::IsWindowFocused();
        panel.m_hovered = ImGui::IsWindowHovered();

        // Before the panel draws, so the actions they queue run in this frame's Draw
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            for (const Command& command : panel.Commands())
                RunIfPressed(command, true);
        }

        panel.m_drawing = true;
        panel.Draw();
        panel.m_drawing = false;

        ImGui::End();

        if (info.Closable)
            panel.m_open = open;
    }
}
