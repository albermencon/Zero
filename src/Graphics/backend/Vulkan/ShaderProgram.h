#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <initializer_list>
#include <span>
#include <vector>

#include "ShaderModule.h"

namespace VoxelEngine {

    // One shader stage: which pipeline stage + which entry point.
    // Does NOT own the ShaderModule — the program or a cache does.
    struct ShaderStage {
        const ShaderModule* module = nullptr;
        vk::ShaderStageFlagBits stage = vk::ShaderStageFlagBits::eVertex;
        const char* entryPoint = "main";

        // Convenience factory helpers
        static ShaderStage vertex(const ShaderModule& m, const char* ep = "vertMain") noexcept;
        static ShaderStage fragment(const ShaderModule& m, const char* ep = "fragMain") noexcept;
        static ShaderStage compute(const ShaderModule& m, const char* ep = "compMain") noexcept;
    };

    // Owns ShaderModules for a complete program (vert + frag, or compute, etc.)
    // and caches the VkPipelineShaderStageCreateInfo array for pipeline creation.
    //
    // Lifetime: keep alive for as long as any pipeline that references it exists.
    //           vk::raii::ShaderModules may be destroyed after pipeline creation —
    //           but we keep them alive here for potential hot-reload later.
    class ShaderProgram {
    public:
        ShaderProgram() = default;

        // Add a pre-built stage (module owned externally)
        void addStage(ShaderStage stage);

        // Convenience: build a vert+frag program from .spv paths
        static ShaderProgram fromFiles(
            const vk::raii::Device& device,
            std::string_view vertSpv,
            std::string_view fragSpv);

        // Convenience: build a vert+frag program from embedded SPIR-V spans
        static ShaderProgram fromEmbedded(
            const vk::raii::Device& device,
            std::span<const char> vertSpv,
            std::span<const char> fragSpv);

        // Returns the array to pass directly to VkGraphicsPipelineCreateInfo
        [[nodiscard]] std::span<const vk::PipelineShaderStageCreateInfo> stageInfos() const noexcept;

        [[nodiscard]] uint32_t stageCount() const noexcept {
            return static_cast<uint32_t>(m_stages.size());
        }

    private:
        // Owned modules — storage indexed parallel to m_stages
        std::vector<ShaderModule>                        m_modules;
        std::vector<ShaderStage>                         m_stages;
        // Rebuilt whenever a stage is added — cheap, avoids per-frame alloc
        std::vector<vk::PipelineShaderStageCreateInfo>   m_stageInfos;

        void rebuildInfos();
    };

}
