#include "crimson/project/shader_includes.hpp"

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <set>
#include <vector>

#include <nlohmann/json.hpp>

#include "crimson/core/engine_resources.hpp"
#include "crimson/core/log.hpp"

namespace fs = std::filesystem;

namespace crimson
{
    static constexpr std::string_view kShaderFolder = "shaders/";
    static constexpr std::string_view kIncludesFolder = "shader_includes";
    static constexpr std::string_view kConfigFileName = "shadertoolsconfig.json";
    static constexpr std::string_view kIncludeDirectoriesKey = "hlsl.additionalIncludeDirectories";

    static std::optional<std::string> ReadText(const fs::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
            return std::nullopt;

        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    static bool WriteText(const fs::path& file, std::string_view text)
    {
        std::error_code error;
        fs::create_directories(file.parent_path(), error);

        std::ofstream stream(file, std::ios::binary);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(stream);
    }

    // Not made read-only, as that gets in the way of deleting the project. A changed copy is put back next time.
    static void WriteHeader(const fs::path& file, std::string_view text)
    {
        if (ReadText(file) != text && !WriteText(file, text))
            LOG_WARN("[Project] Can't write {}", file.string());
    }

    static void RemoveStaleHeaders(const fs::path& folder, const std::set<fs::path>& current)
    {
        std::error_code error;
        std::vector<fs::path> stale;

        for (fs::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
        {
            if (it->is_regular_file(error) && !current.contains(it->path().lexically_normal()))
                stale.push_back(it->path());
        }

        for (const fs::path& file : stale)
            fs::remove(file, error);
    }

    // Leaves the rest of a config the user may have made alone
    static void AddIncludeDirectory(const fs::path& configFile, const std::string& directory)
    {
        nlohmann::ordered_json config = nlohmann::ordered_json::object();

        if (const std::optional<std::string> text = ReadText(configFile))
        {
            config = nlohmann::ordered_json::parse(*text, nullptr, false);

            if (!config.is_object())
            {
                LOG_WARN("[Project] {} isn't valid JSON, so code editors may not find the engine's shader headers", configFile.string());
                return;
            }
        }

        nlohmann::ordered_json& directories = config[kIncludeDirectoriesKey];
        if (!directories.is_array())
            directories = nlohmann::ordered_json::array();

        for (const nlohmann::ordered_json& existing : directories)
        {
            if (existing == directory)
                return;
        }

        directories.push_back(directory);

        if (!WriteText(configFile, config.dump(4) + "\n"))
            LOG_WARN("[Project] Can't write {}", configFile.string());
    }

    void ExportShaderIncludes(const Project& project)
    {
        if (!project.EnsureDataDir())
        {
            LOG_WARN("[Project] Can't create {}", project.DataDir().string());
            return;
        }

        const fs::path folder = project.DataDir() / kIncludesFolder;
        std::set<fs::path> current;

        for (const EmbeddedFile& file : GetEngineResources())
        {
            if (!file.Path.starts_with(kShaderFolder) || !file.Path.ends_with(".hlsli"))
                continue;

            const fs::path copy = folder / fs::path(file.Path.substr(kShaderFolder.size()));
            WriteHeader(copy, file.Text());
            current.insert(copy.lexically_normal());
        }

        RemoveStaleHeaders(folder, current);

        // Relative, for the project to keep working when it is moved
        const std::string directory = fs::relative(folder, project.Root()).generic_string();
        AddIncludeDirectory(project.Root() / kConfigFileName, directory);
    }
}
