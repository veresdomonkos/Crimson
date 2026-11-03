#include "crimson/project/project.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace fs = std::filesystem;

namespace crimson
{
    static constexpr int kProjectVersion = 1;

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
            const nlohmann::json json = nlohmann::json::parse(file);

            const int version = json.value("version", 0);
            if (version < 1 || version > kProjectVersion)
                return std::unexpected("Unsupported project version " + std::to_string(version) + " in " + root.string());

            return Project(json.at("name").get<std::string>(), std::move(root));
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

        std::ofstream file(root / kFileName);
        file << nlohmann::json{ { "name", name }, { "version", kProjectVersion } }.dump(2) << '\n';
        file.close();

        if (!file)
            return std::unexpected("Can't write the project file in " + root.string());

        return Project(name, std::move(root));
    }

    bool Project::EnsureDataDir() const
    {
        const fs::path folder = DataDir();

        std::error_code error;
        fs::create_directories(folder, error);
        if (error)
            return false;

#ifdef _WIN32
        // Also for a folder made before it was hidden
        const DWORD attributes = GetFileAttributesW(folder.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_HIDDEN))
            SetFileAttributesW(folder.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN);
#endif

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
