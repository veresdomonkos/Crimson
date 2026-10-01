#include "editor/editor_application.hpp"

#include <crimson/core/core.hpp>
#include <crimson/core/log.hpp>
#include <crimson/renderer/renderer_api.hpp>

#include "editor/utils.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <glfw/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"

namespace crimson::editor
{
	EditorApplication::EditorApplication(RendererAPIType rendererType)
        : m_running(true)
    {
	    IMGUI_CHECKVERSION();
	    ImGui::CreateContext();
	    ImGuiIO& io = ImGui::GetIO();
	    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	    io.Fonts->AddFontFromFileTTF("assets/fonts/Inter_18pt-Regular.ttf", 18.0f);
	    utils::ApplyEditorStyle();

		m_window = Window::Create(rendererType, WindowData{ "Crimson Editor", 1280, 720, BIND_FN(OnEvent) });
	    m_graphicsBackend = GraphicsBackend::Create(rendererType, *m_window);
	}

    void EditorApplication::Run()
    {
        struct Vertex
        {
            glm::vec3 Position;
            glm::vec3 Normal;
        };

        Vertex floorVertices[] = {
            {{-5.0f, -0.5f, -5.0f}, {0.0f, 1.0f, 0.0f}},
            {{ 5.0f, -0.5f, -5.0f}, {0.0f, 1.0f, 0.0f}},
            {{ 5.0f, -0.5f,  5.0f}, {0.0f, 1.0f, 0.0f}},
            {{-5.0f, -0.5f,  5.0f}, {0.0f, 1.0f, 0.0f}}
        };
        uint32_t floorIndices[] = { 0, 2, 1, 0, 3, 2 };

        VertexBufferInfo floorVInfo{ .Layout = { ShaderDataType::Float3, ShaderDataType::Float3 }, .Size = sizeof(floorVertices), .Usage = BufferUsage::Static };
        VertexBufferHandle floorVB = m_graphicsBackend->GPUResources->CreateVertexBuffer(floorVInfo, floorVertices);

        IndexBufferInfo floorIInfo{ .Size = sizeof(floorIndices), .Usage = BufferUsage::Static, .Type = IndexType::UInt32 };
        IndexBufferHandle floorIB = m_graphicsBackend->GPUResources->CreateIndexBuffer(floorIInfo, floorIndices);

        Vertex pyramidVertices[] = {
            {{ 0.0f,  1.5f,  0.0f}, {0.0f, 1.0f, 0.0f}},
            {{-0.8f,  0.0f,  0.8f}, {-0.7f, 0.5f,  0.7f}},
            {{ 0.8f,  0.0f,  0.8f}, { 0.7f, 0.5f,  0.7f}},
            {{ 0.8f,  0.0f, -0.8f}, { 0.7f, 0.5f, -0.7f}},
            {{-0.8f,  0.0f, -0.8f}, {-0.7f, 0.5f, -0.7f}}
        };
        uint32_t pyramidIndices[] = {
            0, 1, 2,
            0, 2, 3,
            0, 3, 4,
            0, 4, 1
        };

        VertexBufferInfo pyrVInfo{ .Layout = { ShaderDataType::Float3, ShaderDataType::Float3 }, .Size = sizeof(pyramidVertices), .Usage = BufferUsage::Static };
        VertexBufferHandle pyramidVB = m_graphicsBackend->GPUResources->CreateVertexBuffer(pyrVInfo, pyramidVertices);

        IndexBufferInfo pyrIInfo{ .Size = sizeof(pyramidIndices), .Usage = BufferUsage::Static, .Type = IndexType::UInt32 };
        IndexBufferHandle pyramidIB = m_graphicsBackend->GPUResources->CreateIndexBuffer(pyrIInfo, pyramidIndices);

        const char* mainVert = R"(
            // mainVert
            #version 450

            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec3 a_Normal;

            layout(set = 0, binding = 0) uniform CameraBlock {
                mat4 ViewProj;
                vec4 Position;
            } u_Camera;

            layout(location = 0) out vec3 v_Normal;
            layout(location = 1) out vec3 v_WorldPos;

            void main()
            {
                gl_Position = u_Camera.ViewProj * vec4(a_Position, 1.0);
                v_Normal = a_Normal;
                v_WorldPos = a_Position;
            }
        )";

	    const char* mainFrag = R"(
            // mainFrag
            #version 450

            const uint LIGHT_DIRECTIONAL = 0u;
            const uint LIGHT_POINT       = 1u;
            const uint LIGHT_SPOT        = 2u;
            const uint MAX_LIGHTS        = 16u;

            struct GPULight {
                vec4 PositionAndType;
                vec4 DirectionAndRange;
                vec4 ColorAndIntensity;
                vec4 SpotAngles;
            };

            layout(set = 0, binding = 0) uniform CameraBlock {
                mat4 ViewProj;
                vec4 Position;
            } u_Camera;

            layout(set = 0, binding = 1) uniform LightingBlock {
                vec4 AmbientColor;
                mat4 ShadowViewProj;
                uint LightCount;
                int ShadowLightIndex;
                uint _Pad0;
                uint _Pad1;
                GPULight Lights[MAX_LIGHTS];
            } u_Lighting;

            layout(set = 0, binding = 2) uniform sampler2D u_ShadowMap;

            layout(set = 1, binding = 3) uniform MaterialBlock {
                vec4 u_Color;
            } u_Material;

            layout(location = 0) in vec3 v_Normal;
            layout(location = 1) in vec3 v_WorldPos;

            layout(location = 0) out vec4 outColor;

            float ComputeShadow(vec3 worldPos)
            {
                vec4 lightSpacePos = u_Lighting.ShadowViewProj * vec4(worldPos, 1.0);
                vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

                projCoords.xy = projCoords.xy * 0.5 + vec2(0.5);

                if (projCoords.z > 1.0 || any(lessThan(projCoords.xy, vec2(0.0))) || any(greaterThan(projCoords.xy, vec2(1.0))))
                    return 1.0;

                float closestDepth = texture(u_ShadowMap, projCoords.xy).r;
                float bias = 0.005;
                return (projCoords.z - bias > closestDepth) ? 0.35 : 1.0;
            }

            void main()
            {
                vec3 normal = normalize(v_Normal);
                vec3 result = u_Lighting.AmbientColor.rgb;

                for (uint i = 0u; i < u_Lighting.LightCount; ++i)
                {
                    GPULight light = u_Lighting.Lights[i];
                    uint type = uint(light.PositionAndType.w);

                    vec3 lightVec;
                    float attenuation = 1.0;

                    if (type == LIGHT_DIRECTIONAL)
                    {
                        lightVec = -light.DirectionAndRange.xyz;
                    }
                    else
                    {
                        vec3 toLight = light.PositionAndType.xyz - v_WorldPos;
                        float dist = length(toLight);
                        lightVec = toLight / max(dist, 0.0001);

                        float range = light.DirectionAndRange.w;
                        attenuation = clamp(1.0 - (dist / range), 0.0, 1.0);
                        attenuation *= attenuation;

                        if (type == LIGHT_SPOT)
                        {
                            float cosAngle = dot(-lightVec, normalize(light.DirectionAndRange.xyz));
                            float innerCos = light.SpotAngles.x;
                            float outerCos = light.SpotAngles.y;
                            attenuation *= clamp((cosAngle - outerCos) / max(innerCos - outerCos, 0.0001), 0.0, 1.0);
                        }
                    }

                    float diff = max(dot(normal, normalize(lightVec)), 0.0);

                    float shadow = 1.0;
                    if (int(i) == u_Lighting.ShadowLightIndex)
                        shadow = ComputeShadow(v_WorldPos);

                    result += light.ColorAndIntensity.rgb * light.ColorAndIntensity.a * diff * attenuation * shadow;
                }

                outColor = vec4(u_Material.u_Color.rgb * result, 1.0);
            }
        )";

        const char* shadowVert = R"(
            // shadowVert
            #version 450

            layout(location = 0) in vec3 a_Position;

            layout(set = 0, binding = 0) uniform CameraBlock {
                mat4 ViewProj;
                vec4 Position;
            } u_Camera;

            void main()
            {
                gl_Position = u_Camera.ViewProj * vec4(a_Position, 1.0);
            }
        )";

        const char* shadowFrag = R"(
            #version 450
            void main() {}
        )";

        ShaderHandle mainShader = m_graphicsBackend->GPUResources->CreateShader(
            utils::CompileGLSLToSPIRV(mainVert, "vertex"),
            utils::CompileGLSLToSPIRV(mainFrag, "fragment")
        );

        ShaderHandle shadowShader = m_graphicsBackend->GPUResources->CreateShader(
            utils::CompileGLSLToSPIRV(shadowVert, "vertex"),
            utils::CompileGLSLToSPIRV(shadowFrag, "fragment")
        );

        MaterialHandle floorMat = m_graphicsBackend->GPUResources->CreateMaterial(mainShader);
        MaterialHandle pyramidMat = m_graphicsBackend->GPUResources->CreateMaterial(mainShader);
        MaterialHandle shadowMat = m_graphicsBackend->GPUResources->CreateMaterial(shadowShader);

        RenderTargetInfo shadowTargetInfo = { .Width = 2048, .Height = 2048, .DepthFormat = TextureFormat::Depth32F };
        RenderTargetHandle shadowTarget = m_graphicsBackend->GPUResources->CreateRenderTarget(shadowTargetInfo);
        TextureHandle shadowDepth = m_graphicsBackend->GPUResources->GetDepthAttachment(shadowTarget).value();

	    RenderTargetInfo mainTargetInfo{
	        .Width = 1920,
            .Height = 1080,
	        .ColorFormats = {TextureFormat::RGBA8},
            .DepthFormat = TextureFormat::Depth32F
        };

	    RenderTargetHandle mainTarget = m_graphicsBackend->GPUResources->CreateRenderTarget(mainTargetInfo);

	    TextureHandle mainColor = m_graphicsBackend->GPUResources->GetColorAttachment(mainTarget, 0);

        m_graphicsBackend->Renderer->SetShadowMap(shadowDepth);

        m_graphicsBackend->GPUResources->SetMaterialPropertyByName(floorMat, "u_Color", glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
        m_graphicsBackend->GPUResources->SetMaterialPropertyByName(pyramidMat, "u_Color", glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));

        glm::vec3 lightDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
        glm::mat4 lightView = glm::lookAt(-lightDir * 30.0f, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProj = glm::orthoRH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 60.0f);
        glm::mat4 lightViewProj = lightProj * lightView;

	    FrameLightingData lighting{};
	    lighting.CameraPosition = m_camera.GetPosition();
	    lighting.AmbientColor = glm::vec3(0.05f);
	    lighting.ShadowLightIndex = 0;
	    lighting.ShadowViewProj = lightViewProj;

	    lighting.Lights.push_back(Light{
            .Type = LightType::Directional,
            .Direction = lightDir,
            .Color = glm::vec3(1.0f),
            .Intensity = 1.0f
        });

	    lighting.Lights.push_back(Light{
	        .Type = LightType::Point,
	        .Position = glm::vec3(2.0f, 3.0f, 0.0f),
	        .Color = glm::vec3(1.0f, 0.5f, 0.2f),
	        .Intensity = 5.0f,
	        .Range = 8.0f
	    });

	    lighting.Lights.push_back(Light{
           .Type = LightType::Point,
           .Position = glm::vec3(-2.0f, 3.0f, 0.0f),
           .Color = glm::vec3(0.8f, 0.1f, 0.2f),
           .Intensity = 5.0f,
           .Range = 8.0f
       });

        while (m_running)
        {
            const double currentTime = glfwGetTime();
            const auto deltaTime = static_cast<float>(currentTime - m_lastTime);
            m_lastTime = currentTime;

            m_window->PollEvents();

            const auto frameStart = std::chrono::steady_clock::now();
            auto frame = m_graphicsBackend->Renderer->BeginFrame(lighting);
            const auto afterBeginFrame = std::chrono::steady_clock::now();
            m_graphicsBackend->Imgui->NewFrame();

            if (frame.ShouldRender())
            {
                RenderPassInfo shadowPassInfo{
                    .Target = shadowTarget,
                    .ClearFlags = ClearFlags::Depth,
                    .ViewProj = lightViewProj
                };
                auto& shadowPass = frame.BeginRenderPass(shadowPassInfo);
                shadowPass.Draw({floorVB, floorIB, shadowMat});
                shadowPass.Draw({pyramidVB, pyramidIB, shadowMat});

                RenderPassInfo mainPassInfo{
                    .Target = mainTarget,
                    .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
                    .ClearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f),
                    .ViewProj = m_camera.GetViewProj(),
                    .CameraPosition = m_camera.GetPosition()
                };
                auto& mainPass = frame.BeginRenderPass(mainPassInfo);
                mainPass.Draw({floorVB, floorIB, floorMat});
                mainPass.Draw({pyramidVB, pyramidIB, pyramidMat});

                // TEMP FIX
                RenderPassInfo clear {
                    .ClearFlags =  ClearFlags::Color | ClearFlags::Depth,
                    .ClearColor = glm::vec4(0.0f)
                };
                frame.BeginRenderPass(clear);

                frame.AddRawPass(RenderTargetHandle::Invalid(), [this](const NativeFrameHandles& handles) {
                    ImGui::Render();
                    m_graphicsBackend->Imgui->RenderDrawData(ImGui::GetDrawData(), handles);
                });

                const auto afterRender = std::chrono::steady_clock::now();

                ImGuiViewport* viewport = ImGui::GetMainViewport();

                ImGui::SetNextWindowPos(viewport->WorkPos);
                ImGui::SetNextWindowSize(viewport->WorkSize);
                ImGui::SetNextWindowViewport(viewport->ID);

                ImGuiWindowFlags dockFlags =
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                    ImGuiWindowFlags_NoNavFocus |
                    ImGuiWindowFlags_NoBackground;

                ImGui::Begin("DockSpaceHost", nullptr, dockFlags);
                ImGuiID dockspaceId = ImGui::GetID("MyDockSpace");
                ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
                ImGui::End();

                ImTextureID mainTextureId =
                m_graphicsBackend->Imgui->GetOrCreateTextureId(mainColor);

                utils::DrawTextureViewport(
                    "Scene",
                    mainTextureId,
                    16.0f / 9.0f
                );

                ImTextureID shadowMapId = m_graphicsBackend->Imgui->GetOrCreateTextureId(shadowDepth);

                utils::DrawTextureViewport(
                    "Shadow Map",
                    shadowMapId,
                    1.0
                );

                ImGui::Begin("Performance");
                ImGui::Text("FPS: %.1f", m_frameStats.FPS);
                ImGui::Text("Frame Time: %.3f ms", m_frameStats.FrameTimeMs);
                ImGui::Separator();
                ImGui::Text("Render: %.3f ms", m_frameStats.RenderMs);
                ImGui::End();

                m_graphicsBackend->Renderer->EndFrame(frame);
                const auto afterEndFrame = std::chrono::steady_clock::now();

                const float frameTimeMs = std::chrono::duration<float, std::milli>(afterEndFrame - frameStart).count();

                m_frameStats.FrameTimeMs = frameTimeMs;
                m_frameStats.FpsAccumulator += 1000.0f / frameTimeMs;
                ++m_frameStats.FrameCount;
                m_frameStats.UpdateTimer += frameTimeMs / 1000.0f;
                m_frameStats.RenderMs = std::chrono::duration<float, std::milli>(afterEndFrame - afterBeginFrame).count();

                if (m_frameStats.UpdateTimer >= 1.0f)
                {
                    m_frameStats.FPS = m_frameStats.FpsAccumulator / m_frameStats.FrameCount;
                    m_frameStats.FpsAccumulator = 0.0f;
                    m_frameStats.FrameCount = 0;
                    m_frameStats.UpdateTimer = 0.0f;
                }
            }

            HandleMove(deltaTime);
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
