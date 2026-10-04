#include "crimson_editor/importers/shader_importer.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "crimson/core/log.hpp"
#include "crimson_editor/utils.hpp"

namespace crimson::editor
{
    struct ShaderSources
    {
        std::string Vertex;
        std::string Fragment;
    };

    static bool IsTypeDirective(const std::string& line, size_t first)
    {
        constexpr std::string_view kDirective = "#type";

        if (line.compare(first, kDirective.size(), kDirective) != 0)
            return false;

        const size_t next = first + kDirective.size();
        return next < line.size() && (line[next] == ' ' || line[next] == '\t');
    }

    static std::optional<ShaderSources> SplitShaderSources(const std::string& source)
    {
        ShaderSources result;
        std::string* current = nullptr;

        std::istringstream stream(source);
        std::string line;

        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            const size_t first = line.find_first_not_of(" \t");

            if (first != std::string::npos && IsTypeDirective(line, first))
            {
                std::istringstream tokens(line.substr(first + 5));
                std::string stage;
                tokens >> stage;

                if (stage == "vertex")        current = &result.Vertex;
                else if (stage == "fragment") current = &result.Fragment;
                else
                {
                    LOG_ERROR("[ShaderImporter] Unknown shader stage '{}'", stage);
                    return std::nullopt;
                }

                result.Vertex.push_back('\n');
                result.Fragment.push_back('\n');
                continue;
            }

            result.Vertex.append(current == &result.Vertex ? line : std::string{});
            result.Vertex.push_back('\n');
            result.Fragment.append(current == &result.Fragment ? line : std::string{});
            result.Fragment.push_back('\n');
        }

        const auto hasCode = [](const std::string& s)
        {
            return s.find_first_not_of(" \t\r\n") != std::string::npos;
        };

        if (!hasCode(result.Vertex) || !hasCode(result.Fragment))
        {
            LOG_ERROR("[ShaderImporter] Shader file needs both a '#type vertex' and a '#type fragment' section");
            return std::nullopt;
        }

        return result;
    }

    std::span<const std::string_view> ShaderImporter::Extensions() const
    {
        static constexpr std::string_view exts[] = { ".glsl" };
        return exts;
    }

    nlohmann::json ShaderImporter::DefaultSettings() const
    {
        return nlohmann::json::object();
    }

    LoadedAsset ShaderImporter::Load(const AssetMetadata&, const std::filesystem::path& path, ImportContext& ctx)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            LOG_ERROR("[ShaderImporter] Cannot open '{}'", path.string());
            return {};
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        const auto sources = SplitShaderSources(buffer.str());
        if (!sources)
            return {};

        std::vector<uint32_t> vertexSpirv;
        std::vector<uint32_t> fragmentSpirv;

        try
        {
            vertexSpirv   = utils::CompileGLSLToSPIRV(sources->Vertex, "vertex");
            fragmentSpirv = utils::CompileGLSLToSPIRV(sources->Fragment, "fragment");
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("[ShaderImporter] Compilation threw for '{}': {}", path.string(), e.what());
            return {};
        }

        if (vertexSpirv.empty() || fragmentSpirv.empty())
        {
            LOG_ERROR("[ShaderImporter] Compilation failed for '{}'", path.string());
            return {};
        }

        const ShaderHandle handle = ctx.Gpu.CreateShader(vertexSpirv, fragmentSpirv);
        if (!handle)
        {
            LOG_ERROR("[ShaderImporter] CreateShader failed for '{}'", path.string());
            return {};
        }

        return handle;
    }

    void ShaderImporter::Unload(const LoadedAsset& asset, GpuResourceManager& gpu)
    {
        if (const auto* shader = std::get_if<ShaderHandle>(&asset))
            gpu.DestroyShader(*shader);
    }
}