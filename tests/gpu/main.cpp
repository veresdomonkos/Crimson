#include <catch2/catch_session.hpp>
#include <catch2/internal/catch_clara.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "gpu/gpu_test.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;

// The editor keeps its layout and recent projects there, and a test run shouldn't touch the real ones
static void RedirectUserConfig(const std::filesystem::path& folder)
{
#ifdef _WIN32
    _wputenv_s(L"APPDATA", folder.c_str());
#else
    setenv("XDG_CONFIG_HOME", folder.c_str(), 1);
#endif
}

int main(int argc, char* argv[])
{
    Catch::Session session;

    std::string backend = "gl";

    session.cli(session.cli() | Catch::Clara::Opt(backend, "gl|vk")["--backend"]("The graphics backend to test"));

    if (const int result = session.applyCommandLine(argc, argv); result != 0)
        return result;

    if (backend != "gl" && backend != "vk")
    {
        std::cerr << "Unknown backend " << backend << "\n";
        return 1;
    }

    tests::SelectBackend(backend == "vk" ? RendererAPIType::Vulkan : RendererAPIType::OpenGL);

    const tests::TempFolder config;
    RedirectUserConfig(config.Path());

    tests::StartCollectingErrors();

    // Without a working backend every test would fail the same way, and fixtures can't clean up after it
    try
    {
        [[maybe_unused]] GraphicsBackend& gpu = tests::Gpu();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Unexpected exception: can't start the " << backend << " backend: " << e.what() << "\n";
        tests::StopCollectingErrors();
        return 1;
    }

    int result = session.run();

    tests::ShutdownGpu();
    tests::StopCollectingErrors();

    for (const std::string& error : tests::TakeLoggedErrors())
    {
        std::cerr << "Unexpected error: " << error << "\n";
        result = 1;
    }

    return result;
}
