#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "crimson/core/log.hpp"
#include "frame_recorder.hpp"

namespace crimson
{
    enum class PassProblemKind : uint8_t
    {
        ReadsOwnTarget,  // samples a texture of the target it draws to
        ReadBeforeWrite  // samples a texture that a later pass of the frame writes
    };

    struct PassProblem
    {
        uint32_t Pass = 0;
        TextureHandle Texture;
        PassProblemKind Kind = PassProblemKind::ReadsOwnTarget;
        uint32_t Writer = 0; // the pass that writes the texture

        bool operator==(const PassProblem&) const = default;
    };

    // attachmentsOf(RenderTargetHandle) gives the textures a target writes, as a range of TextureHandle.
    // A pass without a target draws to the back buffer, which no pass can read.
    template <typename AttachmentsOf>
    void FindPassProblems(std::span<const PassRecord> passes, const AttachmentsOf& attachmentsOf, std::vector<PassProblem>& problems)
    {
        problems.clear();

        const auto writes = [&](RenderTargetHandle target, TextureHandle texture)
        {
            if (!target)
                return false;

            for (TextureHandle attachment : attachmentsOf(target))
            {
                if (attachment == texture)
                    return true;
            }
            return false;
        };

        for (uint32_t pass = 0; pass < passes.size(); ++pass)
        {
            const PassInfo& info = passes[pass].GetInfo();

            for (const TextureHandle read : info.Reads)
            {
                if (!read)
                    continue;

                if (writes(info.Target, read))
                {
                    problems.push_back({ pass, read, PassProblemKind::ReadsOwnTarget, pass });
                    continue;
                }

                // Reading what an earlier pass wrote is fine, even if a later one writes it again
                bool writtenBefore = false;
                for (uint32_t earlier = 0; earlier < pass && !writtenBefore; ++earlier)
                    writtenBefore = writes(passes[earlier].GetInfo().Target, read);

                if (writtenBefore)
                    continue;

                for (uint32_t later = pass + 1; later < passes.size(); ++later)
                {
                    if (writes(passes[later].GetInfo().Target, read))
                    {
                        problems.push_back({ pass, read, PassProblemKind::ReadBeforeWrite, later });
                        break;
                    }
                }
            }
        }
    }

    // Checks every frame, and logs each problem the first time it shows up rather than every frame
    class FrameValidator
    {
    public:
        template <typename AttachmentsOf>
        void Check(const FramePacket& packet, const AttachmentsOf& attachmentsOf)
        {
            const std::span<const PassRecord> passes = packet.GetPasses();
            FindPassProblems(passes, attachmentsOf, m_problems);

            for (const PassProblem& problem : m_problems)
            {
                const Reported key{ passes[problem.Pass].GetInfo().Name, problem.Texture, problem.Kind };
                if (std::find(m_reported.begin(), m_reported.end(), key) != m_reported.end())
                    continue;

                m_reported.push_back(key);

                if (problem.Kind == PassProblemKind::ReadsOwnTarget)
                    LOG_WARN("[Render Graph] Pass '{}' samples a texture of the target it draws to", key.Pass);
                else
                    LOG_WARN("[Render Graph] Pass '{}' samples a texture that the later pass '{}' writes", key.Pass, passes[problem.Writer].GetInfo().Name);
            }
        }
    private:
        struct Reported
        {
            // A copy, as it is kept for the rest of the run and a pass's name only has to last its frame
            std::string Pass;
            TextureHandle Texture;
            PassProblemKind Kind;

            bool operator==(const Reported&) const = default;
        };

        std::vector<PassProblem> m_problems;
        std::vector<Reported> m_reported;
    };
}
