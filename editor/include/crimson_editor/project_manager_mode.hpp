#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "crimson_editor/app_mode.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor
{
    // The first thing shown: lists the recent projects, and opens, creates or imports one
    class ProjectManagerMode final : public AppMode
    {
    public:
        // The error is shown right away, for when a project couldn't be opened before getting here
        ProjectManagerMode(EditorServices& services, std::string error = {});

        void DrawUI() override;
    private:
        struct ListedProject
        {
            std::string Name;
            std::filesystem::path Path;
            std::string PathText;
            bool Missing = false;
        };

        void RefreshList();

        void DrawHeader();
        void DrawToolbar();
        void DrawProjectList();
        void DrawNewProjectPopup();

        void BeginNewProject();
        void UpdateNewProjectProblem();
        void CreateNewProject();

        void Import();
        void OpenSelected();
        void RemoveSelected();

        EditorServices& m_services;

        std::vector<ListedProject> m_projects;
        std::filesystem::path m_selected;
        ui::SearchFilter m_filter;
        std::string m_error;

        bool m_creating = false;
        std::string m_newName;
        std::string m_newParent;
        std::string m_newProblem;
    };
}
