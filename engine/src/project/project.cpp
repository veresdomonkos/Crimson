#include "crimson/project/project.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "crimson/core/file_write.hpp"
#include "platform/files.hpp"

namespace fs = std::filesystem;

namespace crimson
{
    constexpr int kProjectVersion = 1;

    std::expected<Project, std::string> Project::Load(const fs::path& path)
    {
        std::error_code error;
        fs::path root = fs::absolute(path, error).lexically_normal();

        if (root.filename() == kFileName)
            root = root.parent_path();
        else if (!root.has_filename())
            root = root.parent_path(); // a trailing slash

        std::ifstream file(root / kFileName);
        if (!file)
            return std::unexpected("No " + std::string(kFileName) + " in " + root.string());

        try
        {
            const nlohmann::ordered_json json = nlohmann::ordered_json::parse(file);

            const int version = json.value("version", 0);
            if (version < 1 || version > kProjectVersion)
                return std::unexpected("Unsupported project version " + std::to_string(version) + " in " + root.string());

            // A hand-edited file can't block opening the project, so settings that aren't an object are none
            const auto settings = json.find("settings");
            nlohmann::ordered_json section = settings != json.end() && settings->is_object() ? *settings : nlohmann::ordered_json::object();

            return Project(json.at("name").get<std::string>(), std::move(root), std::move(section));
        }
        catch (const nlohmann::json::exception& e)
        {
            return std::unexpected("Invalid " + std::string(kFileName) + " in " + root.string() + ": " + e.what());
        }
    }

    std::expected<Project, std::string> Project::Create(const fs::path& parentFolder, const std::string& name)
    {
        if (std::string problem = ValidateName(name); !problem.empty())
            return std::unexpected(std::move(problem));

        std::error_code error;
        fs::path root = (fs::absolute(parentFolder, error) / fs::path(name)).lexically_normal();

        if (fs::exists(root, error) && !fs::is_empty(root, error))
            return std::unexpected(root.string() + " already exists and isn't empty");

        fs::create_directories(root / "assets", error);
        if (error)
            return std::unexpected("Can't create " + root.string() + ": " + error.message());

        Project project(name, std::move(root), nlohmann::ordered_json::object());

        if (!project.Save())
            return std::unexpected("Can't write the project file in " + project.m_root.string());

        return project;
    }

    bool Project::Save() const
    {
        nlohmann::ordered_json json = { { "name", m_name }, { "version", kProjectVersion } };

        if (!m_settings.empty())
            json["settings"] = m_settings;

        return WriteFileSafely(m_root / kFileName, json.dump(2) + '\n');
    }

    bool Project::SetSettings(nlohmann::ordered_json settings)
    {
        return SaveWith([&](Project& project) { project.m_settings = std::move(settings); });
    }

    bool Project::EnsureDataDir() const
    {
        const fs::path folder = DataDir();

        std::error_code error;
        fs::create_directories(folder, error);
        if (error)
            return false;

        platform::HideFolder(folder);

        return true;
    }

    std::string Project::ValidateName(std::string_view name)
    {
        if (name.empty())
            return "The name is empty";

        if (name.find_first_of("<>:\"/\\|?*") != std::string_view::npos
            || std::ranges::any_of(name, [](unsigned char c) { return c < 32; }))
            return "The name can't contain any of < > : \" / \\ | ? *";

        // Windows silently drops these from the end of a folder's name
        if (name.back() == '.' || name.back() == ' ' || name.front() == ' ')
            return "The name can't start with a space, or end with a space or a dot";

        std::string upper(name);
        std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c); });

        static constexpr std::array<std::string_view, 22> kReserved = {
            "CON", "PRN", "AUX", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
        };

        if (std::ranges::find(kReserved, std::string_view(upper)) != kReserved.end())
            return "Windows reserves that name";

        return {};
    }
}
