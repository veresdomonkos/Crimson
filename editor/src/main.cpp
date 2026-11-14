#include "crimson_editor/editor_application.hpp"
#include "crimson_editor/platform.hpp"
#include <crimson/core/profiler.hpp>
#include <crimson/graphics/graphics_api.hpp>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[])
{
    const crimson::ProfilerSession profiler;

    crimson::GraphicsApi apiType = crimson::GraphicsApi::OpenGL;
    std::filesystem::path projectPath;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        if ((arg == "-p" || arg == "--project") && i + 1 < argc)
        {
            projectPath = crimson::editor::platform::CommandLineArgumentPath(++i, argv);
        }
        else if (arg == "-vk" || arg == "--vulkan")
        {
            apiType = crimson::GraphicsApi::Vulkan;
        }
        else if (arg == "-gl" || arg == "--opengl")
        {
            apiType = crimson::GraphicsApi::OpenGL;
        }
        else if (arg == "-h" || arg == "--help")
        {
            std::cout << "Usage: CrimsonEditor [options]\n"
                      << "Options:\n"
                      << "  -gl, --opengl    Use OpenGL backend (default)\n"
                      << "  -vk, --vulkan    Use Vulkan backend\n"
                      << "  -p,  --project   The project folder to open\n"
                      << "  -h,  --help      Show this help message\n";
            return 0;
        }
    }

    crimson::editor::EditorApplication app(apiType, projectPath);
    app.Run();

    return 0;
}
