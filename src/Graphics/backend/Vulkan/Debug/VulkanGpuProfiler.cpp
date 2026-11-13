#include "pch.h"

#ifdef ZERO_ENABLE_PROFILING

#include "Graphics/backend/Vulkan/Debug/VulkanGpuProfiler.h"
#include <Engine/Log.h>
#include <cstring>

namespace Zero
{
    namespace
    {
        void CopyZoneName(char (&dst)[48], const char* src) noexcept
        {
            const char* safeSrc = (src && src[0] != '\0') ? src : "Unnamed";
            size_t i = 0;
            for (; i + 1 < sizeof(dst) && safeSrc[i] != '\0'; ++i)
                dst[i] = safeSrc[i];
            dst[i] = '\0';
        }
    }

    void VulkanGpuProfiler::Init(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device& device,
        uint32_t graphicsQueueFamilyIndex)
    {
        Shutdown();

        const auto props = physicalDevice.getProperties();
        const auto queueProps = physicalDevice.getQueueFamilyProperties();

        uint32_t validBits = 0;
        if (graphicsQueueFamilyIndex < queueProps.size())
            validBits = queueProps[graphicsQueueFamilyIndex].timestampValidBits;

        m_timestampPeriodNs = props.limits.timestampPeriod;
        m_supported = (validBits > 0) && (m_timestampPeriodNs > 0.0f);
        if (!m_supported)
        {
            ZERO_CORE_WARN("Vulkan GPU timestamps unsupported on graphics queue family {0}", graphicsQueueFamilyIndex);
            return;
        }

        m_validMask = (validBits >= 64) ? ~0ULL : ((1ULL << validBits) - 1ULL);

        vk::QueryPoolCreateInfo poolInfo{};
        poolInfo.queryType = vk::QueryType::eTimestamp;
        poolInfo.queryCount = MAX_QUERIES_PER_FRAME;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            m_frames[i].queryPool = vk::raii::QueryPool(device, poolInfo);
            ResetFrameState(i);
        }
    }

    void VulkanGpuProfiler::Shutdown()
    {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            ResetFrameState(i);
            m_frames[i].queryPool.clear();
        }
        m_supported = false;
        m_timestampPeriodNs = 0.0f;
        m_validMask = ~0ULL;
    }

    void VulkanGpuProfiler::Collect(uint32_t frameIndex, RHI::FrameTimingStats& output)
    {
        output.zoneCount = 0;
        output.attemptedZoneCount = 0;
        output.gpuFrameMs = 0.0f;
        output.timestampsSupported = m_supported;
        output.truncated = false;

        if (!m_supported || frameIndex >= MAX_FRAMES_IN_FLIGHT)
            return;

        FrameState& frame = m_frames[frameIndex];
        if (!frame.submitted || frame.recordedZoneCount == 0 || !*frame.queryPool)
            return;

        constexpr vk::QueryResultFlags resultFlags =
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait;

        for (uint32_t i = 0; i < frame.recordedZoneCount; ++i)
        {
            const ZoneRecord& zone = frame.zones[i];
            if (!zone.valid)
                continue;

            uint64_t startTs = 0;
            uint64_t endTs = 0;
            if (frame.queryPool.getResults(zone.startQuery, 1, sizeof(uint64_t), &startTs, sizeof(uint64_t), resultFlags) != vk::Result::eSuccess)
                continue;
            if (frame.queryPool.getResults(zone.endQuery, 1, sizeof(uint64_t), &endTs, sizeof(uint64_t), resultFlags) != vk::Result::eSuccess)
                continue;

            const uint64_t deltaTicks = (endTs - startTs) & m_validMask;
            const float durationMs = static_cast<float>(static_cast<double>(deltaTicks) * static_cast<double>(m_timestampPeriodNs) * 1e-6);

            if (output.zoneCount < MAX_ZONES_PER_FRAME)
            {
                RHI::GpuZoneTiming& dst = output.zones[output.zoneCount++];
                std::memcpy(dst.name, zone.name, sizeof(dst.name));
                dst.durationMs = durationMs;
                dst.depth = zone.depth;
            }

            if (std::strcmp(zone.name, "GPU::Frame") == 0)
                output.gpuFrameMs = durationMs;
        }

        output.attemptedZoneCount = frame.attemptedZoneCount;
        output.truncated = (frame.attemptedZoneCount > MAX_ZONES_PER_FRAME);
    }

    void VulkanGpuProfiler::ResetFrameState(uint32_t frameIndex)
    {
        if (frameIndex >= MAX_FRAMES_IN_FLIGHT)
            return;

        FrameState& frame = m_frames[frameIndex];
        for (uint32_t i = 0; i < frame.recordedZoneCount; ++i)
            frame.zones[i].valid = false;
        frame.openDepth = 0;
        frame.overflowDepth = 0;
        frame.activeRecordedDepth = 0;
        frame.attemptedZoneCount = 0;
        frame.recordedZoneCount = 0;
        frame.allocatedQueryCount = 0;
        frame.submitted = false;
    }

    void VulkanGpuProfiler::BeginCommandBuffer(uint32_t frameIndex, vk::raii::CommandBuffer& cmd)
    {
        if (!m_supported || frameIndex >= MAX_FRAMES_IN_FLIGHT)
            return;

        FrameState& frame = m_frames[frameIndex];
        if (!*frame.queryPool)
            return;

        cmd.resetQueryPool(*frame.queryPool, 0, MAX_QUERIES_PER_FRAME);
    }

    void VulkanGpuProfiler::MarkSubmitted(uint32_t frameIndex)
    {
        if (frameIndex >= MAX_FRAMES_IN_FLIGHT)
            return;
        m_frames[frameIndex].submitted = true;
    }

    void VulkanGpuProfiler::BeginZone(uint32_t frameIndex, vk::raii::CommandBuffer& cmd, const char* name)
    {
        if (!m_supported || frameIndex >= MAX_FRAMES_IN_FLIGHT)
            return;

        FrameState& frame = m_frames[frameIndex];
        ++frame.attemptedZoneCount;

        if (frame.openDepth >= MAX_ZONE_DEPTH)
        {
            ++frame.overflowDepth;
            return;
        }

        if (!*frame.queryPool ||
            frame.recordedZoneCount >= MAX_ZONES_PER_FRAME ||
            (frame.allocatedQueryCount + 2) > MAX_QUERIES_PER_FRAME)
        {
            frame.openStack[frame.openDepth++] = INVALID_ZONE;
            return;
        }

        const uint16_t zoneIdx = static_cast<uint16_t>(frame.recordedZoneCount++);
        ZoneRecord& rec = frame.zones[zoneIdx];
        CopyZoneName(rec.name, name);
        rec.depth = frame.activeRecordedDepth++;
        rec.startQuery = static_cast<uint16_t>(frame.allocatedQueryCount++);
        rec.endQuery = 0;
        rec.valid = false;

        cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *frame.queryPool, rec.startQuery);
        frame.openStack[frame.openDepth++] = zoneIdx;
    }

    void VulkanGpuProfiler::EndZone(uint32_t frameIndex, vk::raii::CommandBuffer& cmd)
    {
        if (!m_supported || frameIndex >= MAX_FRAMES_IN_FLIGHT)
            return;

        FrameState& frame = m_frames[frameIndex];
        if (frame.overflowDepth > 0)
        {
            --frame.overflowDepth;
            return;
        }
        if (frame.openDepth == 0)
            return;

        const uint16_t zoneIdx = frame.openStack[--frame.openDepth];
        if (zoneIdx == INVALID_ZONE || zoneIdx >= frame.recordedZoneCount)
            return;

        if (frame.activeRecordedDepth > 0)
            --frame.activeRecordedDepth;

        ZoneRecord& rec = frame.zones[zoneIdx];
        if (!*frame.queryPool || frame.allocatedQueryCount >= MAX_QUERIES_PER_FRAME)
            return;

        rec.endQuery = static_cast<uint16_t>(frame.allocatedQueryCount++);
        cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, *frame.queryPool, rec.endQuery);
        rec.valid = true;
    }
}

#endif
