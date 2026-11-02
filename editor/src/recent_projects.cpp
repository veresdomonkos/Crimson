#include "crimson_editor/recent_projects.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <utility>

#include <crimson/core/log.hpp>
#include <nlohmann/json.hpp>

namespace crimson::editor
{
    RecentProjects::RecentProjects(std::filesystem::path file)
        : m_file(std::move(file))
    {
        Load();
    }

    void RecentProjects::Load()
    {
        std::ifstream file(m_file);
        if (!file)
            return; // a first start

        try
        {
            const nlohmann::json json = nlohmann::json::parse(file);

            for (const nlohmann::json& project : json.at("projects"))
            {
                m_entries.push_back({
                    .Name = project.at("name").get<std::string>(),
                    .Path = std::filesystem::path(project.at("path").get<std::string>()),
                    .LastOpened = project.value("lastOpened", int64_t{ 0 }),
                    .LastScene = AssetID::FromString(project.value("lastScene", std::string("0")))
                });
            }
        }
        catch (const std::exception& e)
        {
            LOG_WARN("[Editor] Ignoring the recent projects in {}: {}", m_file.string(), e.what());
            m_entries.clear();
        }

        std::ranges::stable_sort(m_entries, std::greater{}, &Entry::LastOpened);
    }

    void RecentProjects::Save() const
    {
        nlohmann::json projects = nlohmann::json::array();

        for (const Entry& entry : m_entries)
            projects.push_back({ { "name", entry.Name }, { "path", entry.Path.string() }, { "lastOpened", entry.LastOpened }, { "lastScene", entry.LastScene.ToString() } });

        std::ofstream file(m_file);
        file << nlohmann::json{ { "projects", std::move(projects) } }.dump(2) << '\n';
        file.close();

        if (!file)
            LOG_WARN("[Editor] Can't save the recent projects to {}", m_file.string());
    }

    void RecentProjects::Add(const Project& project)
    {
        const AssetID lastScene = GetLastScene(project.Root());

        std::erase_if(m_entries, [&](const Entry& entry) { return entry.Path == project.Root(); });

        const auto now = std::chrono::system_clock::now().time_since_epoch();
        m_entries.insert(m_entries.begin(), { project.Name(), project.Root(), std::chrono::duration_cast<std::chrono::seconds>(now).count(), lastScene });

        Save();
    }

    void RecentProjects::Remove(const std::filesystem::path& path)
    {
        std::erase_if(m_entries, [&](const Entry& entry) { return entry.Path == path; });
        Save();
    }

    void RecentProjects::SetLastScene(const std::filesystem::path& path, AssetID scene)
    {
        const auto it = std::ranges::find(m_entries, path, &Entry::Path);
        if (it == m_entries.end() || it->LastScene == scene)
            return;

        it->LastScene = scene;
        Save();
    }

    AssetID RecentProjects::GetLastScene(const std::filesystem::path& path) const
    {
        const auto it = std::ranges::find(m_entries, path, &Entry::Path);
        return it != m_entries.end() ? it->LastScene : AssetID{};
    }
}
