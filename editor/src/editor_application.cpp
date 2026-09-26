#include "editor/editor_application.hpp"

#include <crimson/core/core.hpp>
#include <crimson/core/log.hpp>
#include <crimson/renderer/renderer_api.hpp>

#include "editor/utils.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <glfw/glfw3.h>

namespace crimson::editor
{
	EditorApplication::EditorApplication() : m_running(true)
	{
		RendererAPI::Init(RendererAPIType::Vulkan);
		m_window = Window::Create(WindowData{ "My Window", 1280, 720, BIND_FN(OnEvent) });
	    m_renderer = Renderer::Create();
	    m_primarySurface = m_renderer->Initialize(*m_window);
	}

    EditorApplication::~EditorApplication()
    {
        m_renderer->Shutdown();
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
        VertexBufferHandle floorVB = m_renderer->GetResourceManager().CreateVertexBuffer(floorVInfo, floorVertices);

        IndexBufferInfo floorIInfo{ .Size = sizeof(floorIndices), .Usage = BufferUsage::Static, .Type = IndexType::UInt32 };
        IndexBufferHandle floorIB = m_renderer->GetResourceManager().CreateIndexBuffer(floorIInfo, floorIndices);

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
        VertexBufferHandle pyramidVB = m_renderer->GetResourceManager().CreateVertexBuffer(pyrVInfo, pyramidVertices);

        IndexBufferInfo pyrIInfo{ .Size = sizeof(pyramidIndices), .Usage = BufferUsage::Static, .Type = IndexType::UInt32 };
        IndexBufferHandle pyramidIB = m_renderer->GetResourceManager().CreateIndexBuffer(pyrIInfo, pyramidIndices);

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

            #ifndef CRIMSON_FLIP_SHADOW_Y
                #define CRIMSON_FLIP_SHADOW_Y 1  // Vulkan default
            #endif

            float ComputeShadow(vec3 worldPos)
            {
                vec4 lightSpacePos = u_Lighting.ShadowViewProj * vec4(worldPos, 1.0);
                vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

                projCoords.xy = projCoords.xy * 0.5 + vec2(0.5);
                #if CRIMSON_FLIP_SHADOW_Y
                    projCoords.y = 1.0 - projCoords.y;
                #endif

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

        ShaderHandle mainShader = m_renderer->GetResourceManager().CreateShader(
            utils::CompileGLSLToSPIRV(mainVert, "vertex"),
            utils::CompileGLSLToSPIRV(mainFrag, "fragment")
        );

        ShaderHandle shadowShader = m_renderer->GetResourceManager().CreateShader(
            utils::CompileGLSLToSPIRV(shadowVert, "vertex"),
            utils::CompileGLSLToSPIRV(shadowFrag, "fragment")
        );

        MaterialHandle floorMat = m_renderer->GetResourceManager().CreateMaterial(mainShader);
        MaterialHandle pyramidMat = m_renderer->GetResourceManager().CreateMaterial(mainShader);
        MaterialHandle shadowMat = m_renderer->GetResourceManager().CreateMaterial(shadowShader);

        RenderTargetInfo shadowTargetInfo = { .Width = 2048, .Height = 2048, .DepthFormat = TextureFormat::Depth32F };
        RenderTargetHandle shadowTarget = m_renderer->GetResourceManager().CreateRenderTarget(shadowTargetInfo);
        TextureHandle shadowDepth = m_renderer->GetResourceManager().GetDepthAttachment(shadowTarget).value();

        m_renderer->SetShadowMap(shadowDepth);

        m_renderer->GetResourceManager().SetMaterialPropertyByName(floorMat, "u_Color", glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
        m_renderer->GetResourceManager().SetMaterialPropertyByName(pyramidMat, "u_Color", glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));

        glm::vec3 lightDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
        glm::mat4 lightView = glm::lookAt(-lightDir * 30.0f, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProj = RendererAPI::GetType() == RendererAPIType::OpenGL
	        ? glm::orthoRH_NO(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 60.0f)
	        : glm::orthoRH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 60.0f);
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
           .Color = glm::vec3(1.0f, 0.0f, 0.0f),
           .Intensity = 450000000.0f,
           .Range = 5.0f
       });

        while (m_running)
        {
            m_window->PollEvents();

            auto frame = m_renderer->BeginFrame(m_primarySurface, lighting);
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
                    .Target = m_renderer->GetResourceManager().GetCurrentBackBuffer(m_primarySurface),
                    .ClearFlags = ClearFlags::Color | ClearFlags::Depth,
                    .ClearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f),
                    .ViewProj = m_camera.GetViewProj(),
                    .CameraPosition = m_camera.GetPosition()
                };
                auto& mainPass = frame.BeginRenderPass(mainPassInfo);
                mainPass.Draw({floorVB, floorIB, floorMat});
                mainPass.Draw({pyramidVB, pyramidIB, pyramidMat});
            }
            m_renderer->EndFrame(frame);
        }
    }

	void EditorApplication::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);

		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event) {
			m_running = false;
			return true;
		});

	    dispatcher.Dispatch<KeyPressEvent>([this](KeyPressEvent& event) {
	        glm::vec3 direction(0.0f);

	        if (event.GetKeyCode() == GLFW_KEY_A)
	            direction.x -= 0.1f;
	        if (event.GetKeyCode() == GLFW_KEY_D)
                direction.x += 0.1f;
	        if (event.GetKeyCode() == GLFW_KEY_W)
                direction.z -= 0.1f;
	        if (event.GetKeyCode() == GLFW_KEY_S)
                direction.z += 0.1f;
	        if (event.GetKeyCode() == GLFW_KEY_Q)
	            direction.y += 0.1f;
	        if (event.GetKeyCode() == GLFW_KEY_E)
	            direction.y -= 0.1f;

	        m_camera.Move(direction);

	        return true;
        });

	    dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& event)
	    {
	        m_camera.SetAspect(static_cast<float>(event.GetWidth()) / static_cast<float>(event.GetHeight()));
	        return false;
	    });
	}
}
