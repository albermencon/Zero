#pragma once

#ifdef ZERO_ENABLE_PROFILING

#include <array>
#include <cstdint>
#include <Engine/Graphics/RHITypes.h>
#include "Graphics/core/GraphicsCore.h"

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace Zero
{
    class VulkanGpuProfiler
    {
    public:
        static constexpr uint32_t MAX_ZONES_PER_FRAME = RHI::MAX_GPU_PROFILE_ZONES;
        static constexpr uint32_t MAX_QUERIES_PER_FRAME = MAX_ZONES_PER_FRAME * 2;
        static constexpr uint32_t MAX_ZONE_DEPTH = 16;
        static constexpr uint16_t INVALID_ZONE = 0xFFFF;

        struct ZoneRecord
        {
            char name[48]{};
            uint16_t depth = 0;
            uint16_t startQuery = 0;
            uint16_t endQuery = 0;
            bool valid = false;
        };

        void Init(
            const vk::raii::PhysicalDevice& physicalDevice,
            const vk::raii::Device& device,
            uint32_t graphicsQueueFamilyIndex);
        void Shutdown();

        void Collect(uint32_t frameIndex, RHI::FrameTimingStats& output);
        void ResetFrameState(uint32_t frameIndex);
        void BeginCommandBuffer(uint32_t frameIndex, vk::raii::CommandBuffer& cmd);
        void MarkSubmitted(uint32_t frameIndex);

        void BeginZone(uint32_t frameIndex, vk::raii::CommandBuffer& cmd, const char* name);
        void EndZone(uint32_t frameIndex, vk::raii::CommandBuffer& cmd);

        [[nodiscard]] bool IsSupported() const noexcept { return m_supported; }

    private:
        struct FrameState
        {
            vk::raii::QueryPool queryPool{nullptr};
            std::array<ZoneRecord, MAX_ZONES_PER_FRAME> zones{};
            std::array<uint16_t, MAX_ZONE_DEPTH> openStack{};
            uint16_t openDepth = 0;
            uint16_t overflowDepth = 0;
            uint16_t activeRecordedDepth = 0;
            uint32_t attemptedZoneCount = 0;
            uint32_t recordedZoneCount = 0;
            uint32_t allocatedQueryCount = 0;
            bool submitted = false;
        };

        std::array<FrameState, MAX_FRAMES_IN_FLIGHT> m_frames{};
        float m_timestampPeriodNs = 0.0f;
        uint64_t m_validMask = ~0ULL;
        bool m_supported = false;
    };
}

#endif
