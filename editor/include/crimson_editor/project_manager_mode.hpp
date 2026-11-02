#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>

#include "crimson_editor/app_mode.hpp"

namespace crimson::editor
{
    // The first thing shown: lists the recent projects, and opens, creates or imports one
    class ProjectManagerMode final : public AppMode
    {
    public:
        // The error is shown right away, for when a project couldn't be opened before getting here
        ProjectManagerMode(EditorServices& services, std::string error = {});

        void OnImGui() override;
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
        ImGuiTextFilter m_filter;
        std::string m_error;

        bool m_openNewProjectPopup = false;
        std::array<char, 256> m_newName{};
        std::array<char, 1024> m_newParent{};
        std::string m_newProblem;
    };
}
