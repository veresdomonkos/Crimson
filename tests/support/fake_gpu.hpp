#pragma once
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <crimson/renderer/renderer.hpp>
#include <crimson/renderer/resource_manager.hpp>

#include "shader/shader_reflection.hpp"

namespace crimson::tests
{
    // Hands out handles and remembers what it was asked to do, so code above the graphics backends runs without a GPU
    class FakeGpuResourceManager final : public GpuResourceManager
    {
    public:
        struct MaterialRecord
        {
            ShaderHandle Shader;
            MaterialSettings Settings;
            std::map<std::string, std::vector<std::byte>> Properties;
            std::map<std::string, TextureHandle> Textures;
        };

        VertexBufferHandle CreateVertexBuffer(const VertexBufferInfo&, const void*) override { ++LiveVertexBuffers; return VertexBufferHandle{ m_nextId++, 1 }; }
        void DestroyVertexBuffer(VertexBufferHandle) override { --LiveVertexBuffers; }

        IndexBufferHandle CreateIndexBuffer(const IndexBufferInfo&, const void*) override { ++LiveIndexBuffers; return IndexBufferHandle{ m_nextId++, 1 }; }
        void DestroyIndexBuffer(IndexBufferHandle) override { --LiveIndexBuffers; }

        ShaderHandle CreateShader(const ShaderBinary& binary) override
        {
            ++ShadersCreated;
            ++LiveShaders;

            const ShaderHandle handle{ m_nextId++, 1 };
            m_shaderInterfaces[handle.GetRaw()] = ReflectShaderInterface(binary).value_or(ShaderInterface{});
            return handle;
        }

        [[nodiscard]] const ShaderInterface& GetShaderInterface(ShaderHandle handle) const override
        {
            return m_shaderInterfaces.at(handle.GetRaw());
        }

        void DestroyShader(ShaderHandle) override
        {
            --LiveShaders;
            DestroyOrder.emplace_back("shader");
        }

        MaterialHandle CreateMaterial(ShaderHandle shader, const MaterialSettings& settings) override
        {
            const MaterialHandle handle{ m_nextId++, 1 };
            Materials[handle.GetRaw()] = { .Shader = shader, .Settings = settings };
            ++LiveMaterials;
            return handle;
        }

        void DestroyMaterial(MaterialHandle) override
        {
            --LiveMaterials;
            DestroyOrder.emplace_back("material");
        }

        void SetMaterialSettings(MaterialHandle material, const MaterialSettings& settings) override
        {
            Materials[material.GetRaw()].Settings = settings;
        }

        void SetMaterialTexture(MaterialHandle material, std::string_view name, TextureHandle texture) override
        {
            if (texture)
                Materials[material.GetRaw()].Textures[std::string(name)] = texture;
            else
                Materials[material.GetRaw()].Textures.erase(std::string(name));
        }

        TextureHandle CreateTexture(const TextureInfo& info, const void*) override
        {
            LastTextureInfo = info;
            ++LiveTextures;
            return TextureHandle{ m_nextId++, 1 };
        }

        void DestroyTexture(TextureHandle) override { --LiveTextures; }

        std::vector<std::byte> ReadColorPixels(TextureHandle) override { return {}; }

        RenderTargetHandle CreateRenderTarget(const RenderTargetInfo&) override { ++LiveRenderTargets; return RenderTargetHandle{ m_nextId++, 1 }; }
        void DestroyRenderTarget(RenderTargetHandle) override { --LiveRenderTargets; }

        [[nodiscard]] TextureHandle GetColorAttachment(RenderTargetHandle target, uint32_t) const override { return TextureHandle{ target.GetId(), 1 }; }
        [[nodiscard]] std::optional<TextureHandle> GetDepthAttachment(RenderTargetHandle target) const override { return TextureHandle{ target.GetId(), 1 }; }

        [[nodiscard]] bool NothingAlive() const
        {
            return LiveVertexBuffers == 0 && LiveIndexBuffers == 0 && LiveShaders == 0 && LiveMaterials == 0 && LiveTextures == 0 && LiveRenderTargets == 0;
        }

        int LiveVertexBuffers = 0;
        int LiveIndexBuffers = 0;
        int LiveShaders = 0;
        int LiveMaterials = 0;
        int LiveTextures = 0;
        int LiveRenderTargets = 0;

        int ShadersCreated = 0;
        std::vector<std::string> DestroyOrder;
        std::map<uint64_t, MaterialRecord> Materials;
        TextureInfo LastTextureInfo{};
    protected:
        void SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data) override
        {
            Materials[handle.GetRaw()].Properties[std::string(name)] = std::vector<std::byte>(data.begin(), data.end());
        }
    private:
        uint32_t m_nextId = 1;
        std::map<uint64_t, ShaderInterface> m_shaderInterfaces;
    };

    // A renderer that records nothing and only keeps the frame for SceneRenderer to fill
    class FakeRenderer final : public Renderer
    {
    public:
        FrameContext BeginFrame(const FrameLightingData&) override
        {
            CurrentFrame.Reset();
            CurrentFrame.Init(RenderTargetHandle{ 1000, 1 }, true);
            return CurrentFrame.CreateContext();
        }

        void EndFrame(const FrameContext&) override {}

        void SetShadowMap(TextureHandle shadowMap) override { ShadowMap = shadowMap; }

        Frame CurrentFrame;
        TextureHandle ShadowMap;
    };
}
