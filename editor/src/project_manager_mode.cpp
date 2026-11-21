#include "crimson_editor/project_manager_mode.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

#include <crimson/project/project.hpp>

#include "crimson_editor/editor_icons.hpp"
#include "crimson_editor/editor_paths.hpp"
#include "crimson_editor/file_dialog.hpp"
#include "crimson_editor/recent_projects.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/input.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/menus.hpp"
#include "crimson_editor/ui/painter.hpp"
#include "crimson_editor/ui/widgets.hpp"
#include "crimson_editor/ui/window.hpp"

namespace fs = std::filesystem;

namespace crimson::editor
{
    constexpr const char* kLogoIcon = "icons/crimson.png";

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

    void ProjectManagerMode::DrawUI()
    {
        const ui::WindowScope window("Project Manager", ui::WindowKind::Fullscreen);

        DrawHeader();
        DrawToolbar();
        DrawProjectList();
        DrawNewProjectPopup();
    }

    void ProjectManagerMode::DrawHeader()
    {
        const float logoSize = ui::theme::Scaled(ui::theme::SpaceXL + ui::theme::SpaceM);

        {
            ui::Row row;

            if (const TextureHandle logo = m_services.Icons.Get(kLogoIcon))
                ui::Image(logo, glm::vec2(logoSize));

            const ui::FontScope heading(ui::Font::Bold, ui::theme::FontHeading);
            row.Offset((logoSize - ui::LineHeight()) * 0.5f);
            ui::Text("Crimson Projects");
        }

        ui::SetCursor(ui::Cursor() + glm::vec2(0.0f, ui::theme::Scaled(ui::theme::SpaceXS)));
        ui::Separator();
    }

    void ProjectManagerMode::DrawToolbar()
    {
        {
            ui::Row row;

            if (ui::Button("New Project"))
                BeginNewProject();

            if (ui::Button("Import"))
                Import();

            {
                const ui::Disabled disabled(m_selected.empty());

                if (ui::PrimaryButton("Open"))
                    OpenSelected();

                if (ui::Button("Remove from List"))
                    RemoveSelected();
                ui::Tooltip("The project's files stay on disk");
            }

            ui::NextItemFills();
            ui::SearchBox("##Search", m_filter, "Search");
        }

        if (!m_error.empty())
            ui::WrappedText(m_error, ui::theme::Error);
    }

    void ProjectManagerMode::DrawProjectList()
    {
        bool open = false;

        if (const ui::Region region("Projects", {}, true); region)
        {
            if (m_projects.empty())
                ui::Text("No projects yet. Create a new one, or import an existing one.", ui::TextStyle::Muted);

            const float rowHeight = ui::LineHeightWithSpacing() * 2.0f + ui::ItemSpacing().y;

            for (const ListedProject& project : m_projects)
            {
                if (!m_filter.Matches(project.Name) && !m_filter.Matches(project.PathText))
                    continue;

                const ui::IdScope scope(project.PathText);

                if (ui::Selectable("##Row", project.Path == m_selected, ui::SelectableOptions{ .Height = rowHeight, .DoubleClick = true }))
                {
                    m_selected = project.Path;
                    open = ui::input::MouseDoubleClicked(MouseButton::Left);
                }

                // Drawn on top of the row, so the two lines stay part of one clickable item
                const ui::Rect row = ui::item::Bounds();
                const glm::vec2 spacing = ui::ItemSpacing();
                const glm::vec2 namePosition(row.Min.x + spacing.x, row.Min.y + spacing.y * 0.5f);
                const glm::vec2 pathPosition(namePosition.x, namePosition.y + ui::LineHeightWithSpacing());

                ui::Painter painter;
                painter.Text(namePosition, ui::theme::Text, project.Name);

                if (project.Missing)
                    painter.Text(pathPosition, ui::theme::Error, "Missing: " + project.PathText);
                else
                    painter.Text(pathPosition, ui::theme::TextMuted, project.PathText);
            }
        }

        if (open)
            OpenSelected();
    }

    void ProjectManagerMode::BeginNewProject()
    {
        const auto recents = m_services.Recents.GetEntries();
        const fs::path parent = recents.empty() ? paths::DefaultProjectsFolder() : recents.front().Path.parent_path();

        m_newName.clear();
        m_newParent = parent.string();

        UpdateNewProjectProblem();
        m_creating = true;
    }

    void ProjectManagerMode::UpdateNewProjectProblem()
    {
        m_newProblem = Project::ValidateName(m_newName);

        if (!m_newProblem.empty())
            return;

        if (m_newParent.empty())
        {
            m_newProblem = "Choose a location";
            return;
        }

        std::error_code error;
        const fs::path target = fs::path(m_newParent) / fs::path(m_newName);
        if (fs::exists(target, error) && !fs::is_empty(target, error))
            m_newProblem = "That folder already exists and isn't empty";
    }

    void ProjectManagerMode::DrawNewProjectPopup()
    {
        const ui::Modal modal("New Project", "NewProject", [this] { return m_creating; }, ui::ModalOptions{ .ScreenWidth = 0.7f });
        if (!modal)
            return;

        ui::Text("Name");
        ui::NextItemFills();
        if (ui::InputText("##Name", m_newName, ui::InputOptions{ .Focus = modal.IsAppearing() }))
            UpdateNewProjectProblem();

        ui::Text("Location");
        ui::NextItemFills(ui::ButtonWidth("Browse") + ui::ItemSpacing().x);

        {
            const ui::Row row;

            if (ui::InputText("##Location", m_newParent))
                UpdateNewProjectProblem();

            if (ui::Button("Browse"))
            {
                if (const auto folder = PickFolder(m_services.AppWindow, fs::path(m_newParent)))
                {
                    m_newParent = folder->string();
                    UpdateNewProjectProblem();
                }
            }
        }

        ui::Spacing();

        if (m_newProblem.empty())
            ui::Text("Creates " + (fs::path(m_newParent) / fs::path(m_newName)).string(), ui::TextStyle::Muted);
        else
            ui::Text(m_newProblem, ui::theme::Error);

        ui::Spacing();

        bool create = false;
        bool cancel = false;

        {
            const ui::Row row;

            {
                const ui::Disabled disabled(!m_newProblem.empty());
                create = ui::PrimaryButton("Create and Open");
            }

            cancel = ui::Button("Cancel") || ui::input::KeyPressed(Key::Escape);
        }

        if (cancel)
            m_creating = false;

        if (create || (m_newProblem.empty() && ui::input::KeyPressed(Key::Enter)))
        {
            CreateNewProject();
            m_creating = false;
        }
    }

    void ProjectManagerMode::CreateNewProject()
    {
        auto project = Project::Create(fs::path(m_newParent), m_newName);
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
        const fs::path start = recents.empty() ? paths::DefaultProjectsFolder() : recents.front().Path.parent_path();

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
