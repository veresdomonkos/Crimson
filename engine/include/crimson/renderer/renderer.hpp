#pragma once
#include "frame_gpu_data.hpp"
#include "frame_recorder.hpp"
#include "render_stats.hpp"
#include "resource_manager.hpp"

namespace crimson
{
    // A frame goes:
    //
    //     if (!renderer.CanRender())
    //         return;
    //
    //     FrameRecorder frame = renderer.BeginRecording();
    //     ... frame.AddPass(...), frame.Lighting() ...
    //     renderer.Submit(std::move(frame));
    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        // False while there is nothing to render to, like when the window is minimized
        [[nodiscard]] virtual bool CanRender() const = 0;

        // Makes no GPU calls, it only hands out the next packet, emptied. There is one per frame in flight, so record and
        // submit one frame at a time: beginning more than kMaxFramesInFlight at once reuses the first one's packet.
        [[nodiscard]] FrameRecorder BeginRecording()
        {
            FramePacket& packet = m_packets.Next();
            packet.Reset();
            return FrameRecorder(packet);
        }

        // Does the frame's GPU work: waits for its slot, uploads, renders and presents. False when the frame was
        // dropped, like when the swapchain turned out to be out of date.
        virtual bool Submit(FrameRecorder frame) = 0;

        // Bound in every frame, for the shaders that sample shadows. Call between frames.
        virtual void SetShadowAtlas(TextureHandle shadowAtlas) = 0;

        // Stats of the last submitted frame
        [[nodiscard]] RenderStats GetStats() const { return m_lastStats; }

        // How many frames Submit rendered, not counting the dropped ones. What a frame drew into a target that is kept
        // between frames, like the shadow atlas, is only there if it went up by one.
        [[nodiscard]] uint64_t GetSubmittedFrames() const { return m_submittedFrames; }
    protected:
        FramePacketRing<kMaxFramesInFlight> m_packets;
        RenderStats m_stats;
        RenderStats m_lastStats;
        uint64_t m_submittedFrames = 0;
        FrameGpuData m_gpuData;
        FrameClock m_clock;
    };
}
