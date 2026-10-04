#include "editor/editor_application.hpp"

#include <crimson/core/core.hpp>
#include <crimson/core/log.hpp>
#include <crimson/renderer/renderer_api.hpp>

#include "editor/utils.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <glfw/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include "editor/editor_resources.hpp"
#include "editor/ui/asset_browser_panel.hpp"
#include "editor/ui/console_panel.hpp"
#include "editor/ui/hierarchy_panel.hpp"
#include "editor/ui/inspector_panel.hpp"
#include "editor/ui/performance_panel.hpp"
#include "editor/ui/viewport_panel.hpp"
#include "glm/gtx/quaternion.hpp"

namespace crimson::editor
{
	EditorApplication::EditorApplication(RendererAPIType rendererType)
        : m_running(true), m_lastTime(0.0)
    {
        m_window          = Window::Create(rendererType, WindowData{ "Crimson Editor", 1280, 720, BIND_FN(OnEvent) });
        m_graphicsBackend = GraphicsBackend::Create(rendererType, *m_window);
        m_ui              = std::make_unique<ui::EditorUI>(*m_graphicsBackend->Imgui);

        CreateMeshes();
        CreateShadersAndMaterials();
        CreateRenderTargets();
        SetupLighting();
        SetupUI();
    }

    Mesh EditorApplication::CreateMesh(std::span<const std::byte> vertices, std::span<const uint32_t> indices)
    {
        VertexBufferInfo vInfo{ .Layout = kVertexLayout, .Size = vertices.size_bytes(), .Usage = BufferUsage::Static };
        IndexBufferInfo  iInfo{ .Size = indices.size_bytes(), .Usage = BufferUsage::Static, .Type = IndexType::UInt32 };

        return Mesh{
            GpuResources().CreateVertexBuffer(vInfo, vertices.data()),
            GpuResources().CreateIndexBuffer(iInfo, indices.data())
        };
    }

    void EditorApplication::CreateMeshes()
    {
        const Vertex floorVertices[] = {
            {{-5.0f, -0.5f, -5.0f}, {0.0f, 1.0f, 0.0f}},
            {{ 5.0f, -0.5f, -5.0f}, {0.0f, 1.0f, 0.0f}},
            {{ 5.0f, -0.5f,  5.0f}, {0.0f, 1.0f, 0.0f}},
            {{-5.0f, -0.5f,  5.0f}, {0.0f, 1.0f, 0.0f}}
        };
        const uint32_t floorIndices[] = { 0, 2, 1, 0, 3, 2 };

        const Vertex pyramidVertices[] = {
            {{ 0.0f,  1.5f,  0.0f}, { 0.0f, 1.0f,  0.0f}},
            {{-0.8f,  0.0f,  0.8f}, {-0.7f, 0.5f,  0.7f}},
            {{ 0.8f,  0.0f,  0.8f}, { 0.7f, 0.5f,  0.7f}},
            {{ 0.8f,  0.0f, -0.8f}, { 0.7f, 0.5f, -0.7f}},
            {{-0.8f,  0.0f, -0.8f}, {-0.7f, 0.5f, -0.7f}}
        };
        const uint32_t pyramidIndices[] = { 0, 1, 2,  0, 2, 3,  0, 3, 4,  0, 4, 1 };

        m_floor   = CreateMesh(std::as_bytes(std::span(floorVertices)),   floorIndices);
        m_pyramid = CreateMesh(std::as_bytes(std::span(pyramidVertices)), pyramidIndices);
    }

    void EditorApplication::CreateShadersAndMaterials()
    {
        m_mainShader = GpuResources().CreateShader(
            utils::CompileGLSLToSPIRV(kMainVert, "vertex"),
            utils::CompileGLSLToSPIRV(kMainFrag, "fragment"));

        m_shadowShader = GpuResources().CreateShader(
            utils::CompileGLSLToSPIRV(kShadowVert, "vertex"),
            utils::CompileGLSLToSPIRV(kShadowFrag, "fragment"));

        m_floorMat   = GpuResources().CreateMaterial(m_mainShader);
        m_pyramidMat = GpuResources().CreateMaterial(m_mainShader);
        m_shadowMat  = GpuResources().CreateMaterial(m_shadowShader);

        GpuResources().SetMaterialPropertyByName(m_floorMat,   "u_Color", glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
        GpuResources().SetMaterialPropertyByName(m_pyramidMat, "u_Color", glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));
    }

    void EditorApplication::CreateRenderTargets()
    {
        RenderTargetInfo shadowInfo{ .Width = 2048, .Height = 2048, .DepthFormat = TextureFormat::Depth32F };
        m_shadowTarget = GpuResources().CreateRenderTarget(shadowInfo);
        m_shadowDepth  = GpuResources().GetDepthAttachment(m_shadowTarget).value();

        RenderTargetInfo mainInfo{
            .Width = 1920,
            .Height = 1080,
            .ColorFormats = { TextureFormat::RGBA8 },
            .DepthFormat = TextureFormat::Depth32F
        };
        m_mainTarget = GpuResources().CreateRenderTarget(mainInfo);
        m_mainColor  = GpuResources().GetColorAttachment(m_mainTarget, 0);

        m_graphicsBackend->Renderer->SetShadowMap(m_shadowDepth);
    }

    void EditorApplication::SetupLighting()
    {
        const glm::vec3 lightDir  = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
        const glm::mat4 lightView = glm::lookAt(-lightDir * 30.0f, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 lightProj = glm::orthoRH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 60.0f);
        m_lightViewProj = lightProj * lightView;

        m_lighting.CameraPosition   = m_camera.GetPosition();
        m_lighting.AmbientColor     = glm::vec3(0.05f);
        m_lighting.ShadowLightIndex = 0;
        m_lighting.ShadowViewProj   = m_lightViewProj;

        m_lighting.Lights.push_back(Light{
            .Type = LightType::Directional,
            .Direction = lightDir,
            .Color = glm::vec3(1.0f),
            .Intensity = 1.0f
        });
        m_lighting.Lights.push_back(Light{
            .Type = LightType::Point,
            .Position = glm::vec3(2.0f, 3.0f, 0.0f),
            .Color = glm::vec3(1.0f, 0.5f, 0.2f),
            .Intensity = 5.0f,
            .Range = 8.0f
        });
        m_lighting.Lights.push_back(Light{
            .Type = LightType::Point,
            .Position = glm::vec3(-2.0f, 3.0f, 0.0f),
            .Color = glm::vec3(0.8f, 0.1f, 0.2f),
            .Intensity = 5.0f,
            .Range = 8.0f
        });
    }

    void EditorApplication::SetupUI()
    {
        auto& imgui = *m_graphicsBackend->Imgui;

        m_ui->AddPanel<ui::ViewportPanel>("Scene",      imgui.GetOrCreateTextureId(m_mainColor),   16.0f / 9.0f);
        m_ui->AddPanel<ui::ViewportPanel>("Shadow Map", imgui.GetOrCreateTextureId(m_shadowDepth), 1.0f);
        m_ui->AddPanel<ui::PerformancePanel>(m_frameStats);
	    m_ui->AddPanel<ui::ConsolePanel>();
	    m_ui->AddPanel<ui::HierarchyPanel>();
	    m_ui->AddPanel<ui::InspectorPanel>();
	    m_ui->AddPanel<ui::AssetBrowserPanel>();
    }

    void EditorApplication::Run()
    {
        while (m_running)
        {
            const double currentTime = glfwGetTime();
            const auto deltaTime = static_cast<float>(currentTime - m_lastTime);
            m_lastTime = currentTime;

            m_window->PollEvents();
            RenderFrame();
            HandleMove(deltaTime);
        }
    }

    void EditorApplication::RenderFrame()
    {
        using Clock = std::chrono::steady_clock;
        const auto frameStart = Clock::now();

        auto& renderer = *m_graphicsBackend->Renderer;

        auto frame = renderer.BeginFrame(m_lighting);
        const auto afterBeginFrame = Clock::now();

        if (!frame.ShouldRender())
            return;

	    m_graphicsBackend->Imgui->NewFrame();

        RecordShadowPass(frame);
        RecordMainPass(frame);
        RecordUIPass(frame);

        renderer.EndFrame(frame);

        const auto afterEndFrame = Clock::now();
        UpdateFrameStats(
            std::chrono::duration<float, std::milli>(afterEndFrame - frameStart).count(),
            std::chrono::duration<float, std::milli>(afterEndFrame - afterBeginFrame).count());
    }

    void EditorApplication::RecordShadowPass(FrameContext& frame)
    {
        RenderPassInfo info{
            .Target = m_shadowTarget,
            .ClearFlags = ClearFlags::Depth,
            .ViewProj = m_lightViewProj
        };

        auto& pass = frame.BeginRenderPass(info);
        pass.Draw({ m_floor.VB,   m_floor.IB,   m_shadowMat });
        pass.Draw({ m_pyramid.VB, m_pyramid.IB, m_shadowMat });
    }

    void EditorApplication::RecordMainPass(FrameContext& frame)
    {
        RenderPassInfo info{
            .Target = m_mainTarget,
            .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
            .ClearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f),
            .ViewProj = m_camera.GetViewProj(),
            .CameraPosition = m_camera.GetPosition()
        };

        auto& pass = frame.BeginRenderPass(info);
        pass.Draw({ m_floor.VB,   m_floor.IB,   m_floorMat });
        pass.Draw({ m_pyramid.VB, m_pyramid.IB, m_pyramidMat });
    }

    void EditorApplication::RecordUIPass(FrameContext& frame)
    {
        RawPassInfo info{
            .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
            .ClearColor = glm::vec4(0.09f, 0.10f, 0.11f, 1.0f),
            .Callback = [this](const NativeFrameHandles& handles) { m_ui->Draw(handles); }
        };
        frame.AddRawPass(info);
    }

    void EditorApplication::UpdateFrameStats(float frameTimeMs, float renderMs)
    {
        m_frameStats.FrameTimeMs = frameTimeMs;
        m_frameStats.RenderMs    = renderMs;
        m_frameStats.FpsAccumulator += 1000.0f / frameTimeMs;
        ++m_frameStats.FrameCount;
        m_frameStats.UpdateTimer += frameTimeMs / 1000.0f;

        if (m_frameStats.UpdateTimer >= 1.0f)
        {
            m_frameStats.FPS = m_frameStats.FpsAccumulator / static_cast<float>(m_frameStats.FrameCount);
            m_frameStats.FpsAccumulator = 0.0f;
            m_frameStats.FrameCount = 0;
            m_frameStats.UpdateTimer = 0.0f;
        }
    }

    void EditorApplication::HandleMove(float deltaTime)
    {
        auto window = static_cast<GLFWwindow*>(m_window->GetNativeHandle());

        constexpr float moveSpeed = 5.0f;
        constexpr float fastMoveSpeed = 15.0f;
        constexpr float mouseSensitivity = 0.1f;

        static bool rotating = false;
        static bool ignoreMouseDelta = false;

        const bool rightMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        ImGuiIO& io = ImGui::GetIO();

        if (rightMouseDown && !rotating)
        {
            rotating = true;
            ignoreMouseDelta = true;

            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }

        if (!rightMouseDown && rotating)
        {
            rotating = false;
            ignoreMouseDelta = false;

            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);

            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        if (rotating)
        {
            if (ignoreMouseDelta)
            {
                ignoreMouseDelta = false;
            }
            else
            {
                m_camera.Rotate(
                    io.MouseDelta.x * mouseSensitivity,
                    -io.MouseDelta.y * mouseSensitivity
                );
            }
        }

        const float speed = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? fastMoveSpeed : moveSpeed;

        glm::vec3 forward = m_camera.GetForward();
        forward.y = 0.0f;

        if (glm::length2(forward) > 0.0001f)
            forward = glm::normalize(forward);

        const glm::vec3 right = m_camera.GetRight();

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            m_camera.Move(forward * speed * deltaTime);

        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            m_camera.Move(-forward * speed * deltaTime);

        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            m_camera.Move(right * speed * deltaTime);

        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            m_camera.Move(-right * speed * deltaTime);

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            m_camera.Move(glm::vec3(0.0f, speed * deltaTime, 0.0f));

        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            m_camera.Move(glm::vec3(0.0f, -speed * deltaTime, 0.0f));
    }

	void EditorApplication::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);

		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event) {
			m_running = false;
			return true;
		});

	    dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& event)
	    {
	        //m_camera.SetAspect(static_cast<float>(event.GetWidth()) / static_cast<float>(event.GetHeight()));
	        return false;
	    });
	}
}
