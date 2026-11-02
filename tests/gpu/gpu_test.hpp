#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include <crimson/core/window.hpp>
#include <crimson/graphics/graphincs_backend.hpp>
#include <crimson/renderer/frame_context.hpp>

namespace crimson::tests
{
    // The backend this run tests, chosen on the command line
    [[nodiscard]] RendererAPIType SelectedBackend();
    void SelectBackend(RendererAPIType type);

    // The window and backend the rendering tests share, created on first use.
    // Only one can exist at a time, so a test that brings its own has to shut this down first.
    [[nodiscard]] GraphicsBackend& Gpu();
    [[nodiscard]] Window& GpuWindow();
    void ShutdownGpu();

    // Every run fails if something logged an error that no test expected, which includes validation errors
    void StartCollectingErrors();
    void StopCollectingErrors();
    [[nodiscard]] std::vector<std::string> TakeLoggedErrors();

    // For a test that expects errors: removes the ones containing the text, and returns how many there were
    int ExpectLoggedErrors(std::string_view text);

    struct Image
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        std::vector<std::byte> Pixels;

        // Row 0 is at the bottom of clip space
        [[nodiscard]] glm::ivec4 At(uint32_t x, uint32_t row) const;

        // Where a point in clip space lands
        [[nodiscard]] glm::ivec4 AtClip(float x, float y) const;
    };

    [[nodiscard]] bool IsNear(const glm::ivec4& color, const glm::ivec3& expected, int tolerance = 3);

    // A color and depth target to render into and read back
    class OffscreenTarget
    {
    public:
        OffscreenTarget(uint32_t width, uint32_t height);
        ~OffscreenTarget();

        OffscreenTarget(const OffscreenTarget&) = delete;
        OffscreenTarget& operator=(const OffscreenTarget&) = delete;

        [[nodiscard]] RenderTargetHandle Handle() const { return m_target; }
        [[nodiscard]] Image Read() const;
    private:
        RenderTargetHandle m_target;
        TextureHandle m_color;
        uint32_t m_width;
        uint32_t m_height;
    };

    // Runs one whole frame, with whatever record adds to it
    void RenderOneFrame(const std::function<void(FrameContext&)>& record, const FrameLightingData& lighting = {});
}
