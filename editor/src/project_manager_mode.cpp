#include "crimson_editor/project_manager_mode.hpp"

#include <algorithm>
#include <cstring>
#include <system_error>
#include <utility>

#include <crimson/project/project.hpp>

#include "crimson_editor/editor_icons.hpp"
#include "crimson_editor/editor_paths.hpp"
#include "crimson_editor/file_dialog.hpp"
#include "crimson_editor/recent_projects.hpp"
#include "crimson_editor/ui/editor_theme.hpp"

namespace fs = std::filesystem;

namespace crimson::editor
{
    static constexpr const char* kNewProjectPopup = "New Project";
    static constexpr const char* kLogoIcon = "icons/crimson.png";

    ProjectManagerMode::ProjectManagerMode(EditorServices& services, std::string error)
        : m_services(services), m_error(std::move(error))
    {
        RefreshList();
    }

    void ProjectManagerMode::RefreshList()
    {
        m_projects.clear();

        // Once here and not every frame, as it touches the disk
        for (const RecentProjects::Entry& entry : m_services.Recents.GetEntries())
        {
            std::error_code error;
            const bool exists = fs::exists(entry.Path / Project::kFileName, error);

            m_projects.push_back({ entry.Name, entry.Path, entry.Path.string(), !exists });
        }

        const bool stillListed = std::ranges::any_of(m_projects, [this](const ListedProject& project) { return project.Path == m_selected; });
        if (!stillListed)
            m_selected = m_projects.empty() ? fs::path() : m_projects.front().Path;
    }

    void ProjectManagerMode::OnImGui()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);

        // No saved settings, so that the editor's layout stays untouched
        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;

        ImGui::Begin("Project Manager", nullptr, kFlags);

        DrawHeader();
        DrawToolbar();
        DrawProjectList();
        DrawNewProjectPopup();

        ImGui::End();
    }

    void ProjectManagerMode::DrawHeader()
    {
        const float logoSize = ui::theme::Scaled(ui::theme::SpaceXL + ui::theme::SpaceM);

        if (const ImTextureID logo = m_services.Icons.Get(kLogoIcon))
        {
            ImGui::Image(logo, ImVec2(logoSize, logoSize));
            ImGui::SameLine();
        }

        {
            const ui::theme::ScopedFont heading(ui::theme::BoldFont(), ui::theme::FontHeading);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (logoSize - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TextUnformatted("Crimson Projects");
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui::theme::Scaled(ui::theme::SpaceXS));
        ImGui::Separator();
    }

    void ProjectManagerMode::DrawToolbar()
    {
        if (ImGui::Button("New Project"))
            BeginNewProject();

        ImGui::SameLine();
        if (ImGui::Button("Import"))
            Import();

        const bool hasSelection = !m_selected.empty();
        ImGui::BeginDisabled(!hasSelection);

        ImGui::SameLine();
        if (ui::theme::PrimaryButton("Open"))
            OpenSelected();

        ImGui::SameLine();
        if (ImGui::Button("Remove from List"))
            RemoveSelected();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("The project's files stay on disk");

        ImGui::EndDisabled();

        ImGui::SameLine();
        m_filter.Draw("##Search", -FLT_MIN);
        if (!m_filter.IsActive() && !ImGui::IsItemActive())
        {
            const ImVec2 min = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + ImGui::GetStyle().FramePadding.x, min.y + ImGui::GetStyle().FramePadding.y),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), "Search");
        }

        if (!m_error.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::theme::Error);
            ImGui::TextWrapped("%s", m_error.c_str());
            ImGui::PopStyleColor();
        }
    }

    void ProjectManagerMode::DrawProjectList()
    {
        if (!ImGui::BeginChild("Projects", ImVec2(0, 0), ImGuiChildFlags_Borders))
        {
            ImGui::EndChild();
            return;
        }

        if (m_projects.empty())
            ImGui::TextDisabled("No projects yet. Create a new one, or import an existing one.");

        const float rowHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
        bool open = false;

        for (const ListedProject& project : m_projects)
        {
            if (!m_filter.PassFilter(project.Name.c_str()) && !m_filter.PassFilter(project.PathText.c_str()))
                continue;

            ImGui::PushID(project.PathText.c_str());

            if (ImGui::Selectable("##Row", project.Path == m_selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, rowHeight)))
            {
                m_selected = project.Path;
                open = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            }

            // Drawn on top of the row, so the two lines stay part of one clickable item
            const ImGuiStyle& style = ImGui::GetStyle();
            const ImVec2 rowMin = ImGui::GetItemRectMin();
            const ImVec2 namePosition(rowMin.x + style.ItemSpacing.x, rowMin.y + style.ItemSpacing.y * 0.5f);
            const ImVec2 pathPosition(namePosition.x, namePosition.y + ImGui::GetTextLineHeightWithSpacing());
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            drawList->AddText(namePosition, ImGui::GetColorU32(ImGuiCol_Text), project.Name.c_str());

            if (project.Missing)
                drawList->AddText(pathPosition, ImGui::GetColorU32(ui::theme::Error), ("Missing: " + project.PathText).c_str());
            else
                drawList->AddText(pathPosition, ImGui::GetColorU32(ImGuiCol_TextDisabled), project.PathText.c_str());

            ImGui::PopID();
        }

        ImGui::EndChild();

        if (open)
            OpenSelected();
    }

    void ProjectManagerMode::BeginNewProject()
    {
        const auto recents = m_services.Recents.GetEntries();
        const fs::path parent = recents.empty() ? GetDefaultProjectsFolder() : recents.front().Path.parent_path();
        const std::string parentText = parent.string();

        m_newName = {};
        m_newParent = {};
        std::strncpy(m_newParent.data(), parentText.c_str(), m_newParent.size() - 1);

        UpdateNewProjectProblem();
        m_openNewProjectPopup = true;
    }

    void ProjectManagerMode::UpdateNewProjectProblem()
    {
        m_newProblem = Project::ValidateName(m_newName.data());

        if (!m_newProblem.empty())
            return;

        if (m_newParent[0] == '\0')
        {
            m_newProblem = "Choose a location";
            return;
        }

        std::error_code error;
        const fs::path target = fs::path(m_newParent.data()) / fs::path(m_newName.data());
        if (fs::exists(target, error) && !fs::is_empty(target, error))
            m_newProblem = "That folder already exists and isn't empty";
    }

    void ProjectManagerMode::DrawNewProjectPopup()
    {
        if (m_openNewProjectPopup)
        {
            ImGui::OpenPopup(kNewProjectPopup);
            m_openNewProjectPopup = false;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.7f, 0.0f), ImGuiCond_Appearing);

        if (!ImGui::BeginPopupModal(kNewProjectPopup, nullptr, ImGuiWindowFlags_NoSavedSettings))
            return;

        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();

        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##Name", m_newName.data(), m_newName.size()))
            UpdateNewProjectProblem();

        ImGui::TextUnformatted("Location");
        const float browseWidth = ImGui::CalcTextSize("Browse").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(-(browseWidth + ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::InputText("##Location", m_newParent.data(), m_newParent.size()))
            UpdateNewProjectProblem();

        ImGui::SameLine();
        if (ImGui::Button("Browse"))
        {
            if (const auto folder = PickFolder(m_services.AppWindow, fs::path(m_newParent.data())))
            {
                const std::string text = folder->string();
                std::strncpy(m_newParent.data(), text.c_str(), m_newParent.size() - 1);
                m_newParent.back() = '\0';
                UpdateNewProjectProblem();
            }
        }

        ImGui::Spacing();

        if (m_newProblem.empty())
        {
            const std::string target = (fs::path(m_newParent.data()) / fs::path(m_newName.data())).string();
            ImGui::TextDisabled("Creates %s", target.c_str());
        }
        else
        {
            ImGui::TextColored(ui::theme::Error, "%s", m_newProblem.c_str());
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(!m_newProblem.empty());
        const bool create = ui::theme::PrimaryButton("Create and Open");
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();

        if (create || (m_newProblem.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
        {
            CreateNewProject();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void ProjectManagerMode::CreateNewProject()
    {
        auto project = Project::Create(fs::path(m_newParent.data()), m_newName.data());
        if (!project)
        {
            m_error = project.error();
            return;
        }

        m_error.clear();
        m_services.OpenProject(std::move(*project));
    }

    void ProjectManagerMode::Import()
    {
        const auto recents = m_services.Recents.GetEntries();
        const fs::path start = recents.empty() ? GetDefaultProjectsFolder() : recents.front().Path.parent_path();

        const auto folder = PickFolder(m_services.AppWindow, start);
        if (!folder)
            return;

        const auto project = Project::Load(*folder);
        if (!project)
        {
            m_error = project.error();
            return;
        }

        m_error.clear();
        m_services.Recents.Add(*project);
        m_selected = project->Root();
        RefreshList();
    }

    void ProjectManagerMode::OpenSelected()
    {
        auto project = Project::Load(m_selected);
        if (!project)
        {
            // It may have been moved or fixed since the list was made
            m_error = project.error();
            RefreshList();
            return;
        }

        m_error.clear();
        m_services.OpenProject(std::move(*project));
    }

    void ProjectManagerMode::RemoveSelected()
    {
        m_services.Recents.Remove(m_selected);
        m_selected.clear();
        m_error.clear();
        RefreshList();
    }
}
