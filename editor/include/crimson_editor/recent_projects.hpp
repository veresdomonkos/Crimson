#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <crimson/asset_manager/asset_id.hpp>
#include <crimson/project/project.hpp>

namespace crimson::editor
{
    // The projects the project manager lists, most recently opened first. Saved on every change.
    class RecentProjects
    {
    public:
        struct Entry
        {
            std::string Name;
            std::filesystem::path Path;
            int64_t LastOpened = 0; // seconds since the epoch
            AssetID LastScene; // what to open next time. Kept here for it not to change the project's files.
        };

        explicit RecentProjects(std::filesystem::path file);

        [[nodiscard]] std::span<const Entry> GetEntries() const { return m_entries; }

        // Adds the project, or moves it to the top if it is already listed
        void Add(const Project& project);

        // Only forgets it, the project's files stay where they are
        void Remove(const std::filesystem::path& path);

        void SetLastScene(const std::filesystem::path& path, AssetID scene);
        [[nodiscard]] AssetID GetLastScene(const std::filesystem::path& path) const;
    private:
        void Load();
        void Save() const;

        std::filesystem::path m_file;
        std::vector<Entry> m_entries;
    };
}
