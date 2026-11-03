#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace crimson
{
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

        // What the engine makes for the project itself, which can always be made again
        [[nodiscard]] std::filesystem::path DataDir() const { return m_root / ".crimson"; }
    private:
        Project(std::string name, std::filesystem::path root) : m_name(std::move(name)), m_root(std::move(root)) {}

        std::string m_name;
        std::filesystem::path m_root;
    };
}
