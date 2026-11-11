#include "gpu/gpu_test.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include <crimson/core/log.hpp>

namespace crimson::tests
{
    namespace
    {
        GraphicsApi s_backend = GraphicsApi::OpenGL;

        // In this order, as the backend needs the window while it is destroyed
        std::unique_ptr<Window> s_window;
        std::unique_ptr<GraphicsBackend> s_graphics;

        uint64_t s_errorSubscription = 0;
        std::vector<std::string> s_errors;

        void EnsureGpu()
        {
            if (s_graphics)
                return;

            s_window = Window::Create(s_backend, WindowData{ "Crimson GPU tests", 320, 240, [](Event&) {} });
            s_graphics = GraphicsBackend::Create(s_backend, *s_window);
        }
    }

    GraphicsApi SelectedBackend() { return s_backend; }
    void SelectBackend(GraphicsApi type) { s_backend = type; }

    GraphicsBackend& Gpu()
    {
        EnsureGpu();
        return *s_graphics;
    }

    Window& GpuWindow()
    {
        EnsureGpu();
        return *s_window;
    }

    void ShutdownGpu()
    {
        s_graphics.reset();
        s_window.reset();
    }

    void StartCollectingErrors()
    {
        s_errorSubscription = Logger::Subscribe([](LogLevel level, const std::string& message)
        {
            if (level == LogLevel::Error)
                s_errors.push_back(message);
        });
    }

    void StopCollectingErrors()
    {
        Logger::Unsubscribe(s_errorSubscription);
    }

    std::vector<std::string> TakeLoggedErrors()
    {
        return std::exchange(s_errors, {});
    }

    int ExpectLoggedErrors(std::string_view text)
    {
        return static_cast<int>(std::erase_if(s_errors, [text](const std::string& error) { return error.find(text) != std::string::npos; }));
    }

    glm::ivec4 Image::At(uint32_t x, uint32_t row) const
    {
        const size_t index = (static_cast<size_t>(row) * Width + x) * 4;

        return {
            std::to_integer<int>(Pixels[index]),
            std::to_integer<int>(Pixels[index + 1]),
            std::to_integer<int>(Pixels[index + 2]),
            std::to_integer<int>(Pixels[index + 3])
        };
    }

    glm::ivec4 Image::AtClip(float x, float y) const
    {
        const auto toPixel = [](float value, uint32_t size)
        {
            return std::clamp(static_cast<int>((value * 0.5f + 0.5f) * static_cast<float>(size)), 0, static_cast<int>(size) - 1);
        };

        return At(static_cast<uint32_t>(toPixel(x, Width)), static_cast<uint32_t>(toPixel(y, Height)));
    }

    bool IsNear(const glm::ivec4& color, const glm::ivec3& expected, int tolerance)
    {
        return std::abs(color.r - expected.r) <= tolerance
            && std::abs(color.g - expected.g) <= tolerance
            && std::abs(color.b - expected.b) <= tolerance;
    }

    float LinearBrightness(const glm::ivec4& color)
    {
        const auto decode = [](int channel)
        {
            const float encoded = static_cast<float>(channel) / 255.0f;
            return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        };

        return (decode(color.r) + decode(color.g) + decode(color.b)) / 3.0f;
    }

    std::string MakeBmp(uint32_t width, uint32_t height, const std::vector<Rgb>& pixels)
    {
        const auto append = [](std::string& out, uint32_t value, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
                out.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
        };

        const uint32_t rowSize = (width * 3 + 3) / 4 * 4;

        std::string out = "BM";
        append(out, 54 + rowSize * height, 4);
        append(out, 0, 4);
        append(out, 54, 4);
        append(out, 40, 4);
        append(out, width, 4);
        append(out, height, 4);
        append(out, 1, 2);
        append(out, 24, 2);
        append(out, 0, 4);
        append(out, rowSize * height, 4);
        append(out, 0, 16);

        for (uint32_t row = height; row-- > 0;)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const Rgb& pixel = pixels[row * width + x];
                out.push_back(static_cast<char>(pixel.B));
                out.push_back(static_cast<char>(pixel.G));
                out.push_back(static_cast<char>(pixel.R));
            }

            out.append(rowSize - width * 3, '\0');
        }

        return out;
    }

    OffscreenTarget::OffscreenTarget(uint32_t width, uint32_t height)
        : m_width(width), m_height(height)
    {
        const RenderTargetInfo info{
            .Width = width,
            .Height = height,
            .ColorFormats = { TextureFormat::RGBA8 },
            .DepthFormat = TextureFormat::Depth32F
        };

        m_target = Gpu().GPUResources->CreateRenderTarget(info);
        m_color = Gpu().GPUResources->GetColorAttachment(m_target, 0);
    }

    OffscreenTarget::~OffscreenTarget()
    {
        Gpu().Device->WaitIdle();
        Gpu().GPUResources->DestroyRenderTarget(m_target);
    }

    Image OffscreenTarget::Read() const
    {
        return { m_width, m_height, Gpu().GPUResources->ReadColorPixels(m_color) };
    }

    void RenderOneFrame(const std::function<void(FrameRecorder&)>& record, const FrameLighting& lighting)
    {
        constexpr int kAttempts = 10;
        Renderer& renderer = *Gpu().Renderer;

        // A frame can be skipped, for example when the swapchain was out of date and got recreated
        for (int attempt = 0; attempt < kAttempts; ++attempt)
        {
            GpuWindow().PollEvents();

            if (!renderer.CanRender())
                continue;

            FrameRecorder frame = renderer.BeginRecording();
            frame.Lighting() = lighting;

            record(frame);

            // Something has to present the back buffer, and a native pass is what leaves it ready for that
            frame.AddNativePass({ .Name = "Present", .Clear = { .Flags = ClearFlags::Color } }, [](const NativeContext&) {});

            if (renderer.Submit(std::move(frame)))
                return;
        }

        FAIL("The renderer kept skipping frames");
    }
}
