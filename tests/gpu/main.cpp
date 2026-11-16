#include <catch2/catch_session.hpp>
#include <catch2/internal/catch_clara.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include <crimson/core/profiler.hpp>
#include <crimson/renderer/shader_cache.hpp>

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

// The compiled shaders of the whole run are kept in one folder, the one CRIMSON_SHADER_CACHE names, as CI does to keep
// them between runs. Without it they go in the run's own folder, and never in the user's real cache.
static std::filesystem::path RedirectShaderCache(const std::filesystem::path& fallback)
{
    if (const char* chosen = std::getenv("CRIMSON_SHADER_CACHE"); chosen && *chosen)
        return chosen;

#ifdef _WIN32
    _wputenv_s(L"CRIMSON_SHADER_CACHE", fallback.c_str());
#else
    setenv("CRIMSON_SHADER_CACHE", fallback.c_str(), 1);
#endif

    return fallback;
}

int main(int argc, char* argv[])
{
    const ProfilerSession profiler;

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

    tests::SelectBackend(backend == "vk" ? GraphicsApi::Vulkan : GraphicsApi::OpenGL);

    const tests::TempFolder config;
    RedirectUserConfig(config.Path());

    // The tests that don't start an editor use it too
    ShaderCache::Engine().SetFolder(RedirectShaderCache(config.Path() / "shader_cache"));

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
