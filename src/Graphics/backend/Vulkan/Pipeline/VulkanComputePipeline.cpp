#include "pch.h"
#include "Graphics/backend/Vulkan/Pipeline/VulkanComputePipeline.h"
#include "Graphics/backend/Vulkan/Shader/ShaderModule.h"
#include "Graphics/backend/Vulkan/Device/VulkanLogicalDevice.h"
#include "Graphics/backend/Vulkan/Translator/VulkanTranslator.h"

#include <vector>
#include <stdexcept>

namespace Zero
{
    VulkanComputePipeline::VulkanComputePipeline(VulkanLogicalDevice* device, const ComputePipelineDesc& desc)
        : m_device(device)
    {
#if defined(ZERO_DEBUG) || defined(ZERO_ENABLE_PROFILING)
        SetDebugName(desc.debugName, "ComputePipeline");
#endif
        createComputePipeline(desc);
    }

    void VulkanComputePipeline::createComputePipeline(const ComputePipelineDesc& desc)
    {
        if (!desc.computeShader.path)
        {
            throw std::runtime_error("Compute pipeline requires a valid compute shader path.");
        }

        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        bindings.reserve(desc.bindings.size());
        for (const auto& b : desc.bindings)
        {
            bindings.push_back(vk::DescriptorSetLayoutBinding{
                b.binding,
                Vulkan::toVkDescriptorType(b.type),
                b.count,
                Vulkan::toVkShaderStageFlags(b.stages)
            });
        }

        vk::DescriptorSetLayoutCreateInfo descLayoutInfo{};
        descLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        descLayoutInfo.pBindings = bindings.data();

        m_descriptorSetLayout = vk::raii::DescriptorSetLayout(m_device->Get(), descLayoutInfo);
        vk::DescriptorSetLayout rawLayout = *m_descriptorSetLayout;

        std::vector<vk::PushConstantRange> pushConstants;
        pushConstants.reserve(desc.pushConstants.size());
        m_pushConstantSize = 0;
        for (const auto& pc : desc.pushConstants)
        {
            m_pushConstantSize += pc.size;
            pushConstants.push_back(vk::PushConstantRange{
                Vulkan::toVkShaderStageFlags(pc.stages),
                pc.offset,
                pc.size
            });
        }

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        if (!bindings.empty())
        {
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &rawLayout;
        }
        if (!pushConstants.empty())
        {
            pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
            pipelineLayoutInfo.pPushConstantRanges = pushConstants.data();
        }

        m_pipelineLayout = vk::raii::PipelineLayout(m_device->Get(), pipelineLayoutInfo);

        auto computeSource = SpirVSource::fromFile(desc.computeShader.path);
        auto computeModule = ShaderModule(m_device->Get(), std::move(computeSource));
        const char* ep = desc.computeShader.entryPoint ? desc.computeShader.entryPoint : "main";

        vk::PipelineShaderStageCreateInfo stageInfo{};
        stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
        stageInfo.module = computeModule.handle();
        stageInfo.pName = ep;

        vk::ComputePipelineCreateInfo computeInfo{};
        computeInfo.stage = stageInfo;
        computeInfo.layout = *m_pipelineLayout;

        m_pipeline = vk::raii::Pipeline(m_device->Get(), nullptr, computeInfo);
    }
}

