#include "crimson_editor/ui/editor_ui.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <utility>

namespace crimson::editor::ui
{
    EditorUI::EditorUI(MainMenuActions actions)
        : m_actions(std::move(actions))
    {
    }

    EditorUI::~EditorUI()
    {
        for (auto panel : m_panels)
            delete panel;
    }

    void EditorUI::Draw()
    {
        ImGuizmo::BeginFrame();

        DrawMainMenu();
        DrawDockSpace();
        for (auto panel : m_panels)
            panel->OnImGui();

        // Above the panels
        m_notifications.Draw();
    }

    void EditorUI::DrawMainMenu()
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save All", "Ctrl+S"))
                m_actions.SaveAll();

            if (ImGui::MenuItem("Close Project"))
                m_actions.CloseProject();

            ImGui::EndMenu();
        }

        // Before the panels, whose Edit items ImGui adds to this menu after these
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_actions.CanUndo()))
                m_actions.Undo();

            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_actions.CanRedo()))
                m_actions.Redo();

            ImGui::Separator();
            ImGui::EndMenu();
        }

        for (Panel* panel : m_panels)
            panel->OnMainMenuBar();

        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Reset Layout"))
                m_resetLayout = true;

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    void EditorUI::BuildDefaultLayout(ImGuiID dockSpace, const ImVec2& size)
    {
        ImGui::DockBuilderRemoveNode(dockSpace);
        ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(dockSpace, size);

        ImGuiID center = dockSpace;
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f, nullptr, &center);
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.35f, nullptr, &right);

        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Scene", center);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);

        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Shadow Map", rightBottom);
        ImGui::DockBuilderDockWindow("Render Settings", rightBottom);
        ImGui::DockBuilderDockWindow("Stats", rightBottom);

        ImGui::DockBuilderFinish(dockSpace);
    }

    void EditorUI::DrawDockSpace()
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
            BuildDefaultLayout(dockSpace, viewport->WorkSize);
            m_resetLayout = false;
        }

        ImGui::Begin("DockSpaceHost", nullptr, flags);
        ImGui::DockSpace(dockSpace, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();
    }
}
