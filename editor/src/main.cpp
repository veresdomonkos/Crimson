#include "crimson_editor/editor_application.hpp"
#include <crimson/renderer/renderer_api.hpp>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[])
{
    crimson::RendererAPIType apiType = crimson::RendererAPIType::OpenGL;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        if (arg == "-vk" || arg == "--vulkan")
        {
            apiType = crimson::RendererAPIType::Vulkan;
        }
        else if (arg == "-gl" || arg == "--opengl")
        {
            apiType = crimson::RendererAPIType::OpenGL;
        }
        else if (arg == "-h" || arg == "--help")
        {
            std::cout << "Usage: CrimsonEditor [options]\n"
                      << "Options:\n"
                      << "  -gl, --opengl    Use OpenGL backend (default)\n"
                      << "  -vk, --vulkan    Use Vulkan backend\n"
                      << "  -h,  --help      Show this help message\n";
            return 0;
        }
    }

    crimson::editor::EditorApplication app(apiType);
    app.Run();

    return 0;
}