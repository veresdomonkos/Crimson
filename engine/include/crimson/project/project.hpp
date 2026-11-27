#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "crimson/asset_manager/asset_id.hpp"

namespace crimson
{
    // The scene a built game starts with, as the project file holds it. It is a raw reference, as loading the project
    // comes before there are assets to look it up in.
    struct MainSceneRef
    {
        AssetID Id;
        std::string Path; // relative to the assets folder, to find it by if the id is stale
    };

    // A folder with a project file and an assets folder. The errors are meant to be shown to the user.
    class Project
    {
    public:
        static constexpr std::string_view kFileName = "project.crimson";

        // Takes the project's folder or its project file
        [[nodiscard]] static std::expected<Project, std::string> Load(const std::filesystem::path& path);

        // Creates parentFolder/name. Fails if that already exists with something in it.
        [[nodiscard]] static std::expected<Project, std::string> Create(const std::filesystem::path& parentFolder, const std::string& name);

        // Empty if the name can be used for a project's folder, otherwise what is wrong with it
        [[nodiscard]] static std::string ValidateName(std::string_view name);

        [[nodiscard]] const std::string& Name() const { return m_name; }
        [[nodiscard]] const std::filesystem::path& Root() const { return m_root; }
        [[nodiscard]] std::filesystem::path AssetsDir() const { return m_root / "assets"; }

        // What the editor keeps for the project apart from its assets, such as caches and recovery copies
        [[nodiscard]] std::filesystem::path DataDir() const { return m_root / ".crimson"; }

        // Creates the data folder if it isn't there yet. On Windows it is also hidden, as the dot only hides it elsewhere.
        // Returns false if it can't be created.
        bool EnsureDataDir() const;

        // Both empty if there is none
        [[nodiscard]] const MainSceneRef& GetMainScene() const { return m_mainScene; }

        // An empty id clears it. Writes the project file, and returns false, keeping the old one, if that fails. Logs
        // nothing, for the caller to report.
        [[nodiscard]] bool SetMainScene(AssetID id, std::string path);
    private:
        Project(std::string name, std::filesystem::path root, MainSceneRef mainScene)
            : m_name(std::move(name)), m_root(std::move(root)), m_mainScene(std::move(mainScene)) {}

        [[nodiscard]] bool Save() const;

        std::string m_name;
        std::filesystem::path m_root;
        MainSceneRef m_mainScene;
    };
}
