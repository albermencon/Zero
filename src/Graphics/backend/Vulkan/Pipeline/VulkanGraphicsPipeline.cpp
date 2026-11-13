#include "pch.h"
#include "Graphics/backend/Vulkan/Pipeline/VulkanGraphicsPipeline.h"
#include "Graphics/backend/Vulkan/Shader/ShaderModule.h"
#include "Graphics/backend/Vulkan/Shader/ShaderProgram.h"
#include "Graphics/backend/Vulkan/Device/VulkanLogicalDevice.h"
#include "Graphics/backend/Vulkan/Translator/VulkanTranslator.h"

#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace Zero
{
    VulkanGraphicsPipeline::VulkanGraphicsPipeline(VulkanLogicalDevice* device, const GraphicsPipelineDesc& desc)
        : m_device(device)
    {
#if defined(ZERO_DEBUG) || defined(ZERO_ENABLE_PROFILING)
        SetDebugName(desc.debugName, "GraphicsPipeline");
#endif
        createFromDesc(desc);
    }

    void VulkanGraphicsPipeline::createFromDesc(const GraphicsPipelineDesc& desc)
    {
        ShaderProgram shaderProgram;
        std::vector<ShaderModule> ownedModules;
        ownedModules.reserve(8);

        if (desc.shaders.hasVertex())
        {
            auto vertSource = SpirVSource::fromFile(desc.shaders.vertex.path);
            ownedModules.emplace_back(m_device->Get(), std::move(vertSource));
            const char* ep = desc.shaders.vertex.entryPoint ? desc.shaders.vertex.entryPoint : "main";
            shaderProgram.addStage(ShaderStage::vertex(ownedModules.back(), ep));
        }

        if (desc.shaders.hasFragment())
        {
            auto fragSource = SpirVSource::fromFile(desc.shaders.fragment.path);
            ownedModules.emplace_back(m_device->Get(), std::move(fragSource));
            const char* ep = desc.shaders.fragment.entryPoint ? desc.shaders.fragment.entryPoint : "main";
            shaderProgram.addStage(ShaderStage::fragment(ownedModules.back(), ep));
        }

        if (shaderProgram.stageCount() == 0)
        {
            throw std::runtime_error("Graphics pipeline requires at least one shader stage.");
        }

        std::vector<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

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
        m_pushConstantStages = {};
        for (const auto& pc : desc.pushConstants)
        {
            m_pushConstantSize = std::max(m_pushConstantSize, pc.offset + pc.size);
            m_pushConstantStages |= Vulkan::toVkShaderStageFlags(pc.stages);
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

        std::vector<vk::Format> colorFormats;
        colorFormats.reserve(desc.renderTargetLayout.colorCount);
        for (uint32_t i = 0; i < desc.renderTargetLayout.colorCount; ++i)
        {
            colorFormats.push_back(Vulkan::toVkFormat(desc.renderTargetLayout.colorFormats[i]));
        }

        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
        pipelineRenderingCreateInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
        pipelineRenderingCreateInfo.pColorAttachmentFormats = colorFormats.data();
        if (desc.renderTargetLayout.depthStencilFormat != PixelFormat::Undefined)
        {
            pipelineRenderingCreateInfo.depthAttachmentFormat = Vulkan::toVkFormat(desc.renderTargetLayout.depthStencilFormat);
        }

        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        bindingDescriptions.reserve(desc.vertexLayout.numBindings());
        for (uint32_t i = 0; i < desc.vertexLayout.numBindings(); ++i)
        {
            const auto& b = desc.vertexLayout.bindingData()[i];
            bindingDescriptions.push_back(vk::VertexInputBindingDescription{
                b.binding,
                b.stride,
                Vulkan::toVkInputRate(b.inputRate)
            });
        }

        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
        attributeDescriptions.reserve(desc.vertexLayout.numAttributes());
        for (uint32_t i = 0; i < desc.vertexLayout.numAttributes(); ++i)
        {
            const auto& a = desc.vertexLayout.attributeData()[i];
            attributeDescriptions.push_back(vk::VertexInputAttributeDescription{
                a.location,
                a.binding,
                Vulkan::toVkFormat(a.format),
                a.offset
            });
        }

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = Vulkan::toVkTopology(desc.topology);
        inputAssembly.primitiveRestartEnable = desc.primitiveRestart ? VK_TRUE : VK_FALSE;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = desc.rasterizerState.depthClampEnable ? vk::True : vk::False;
        rasterizer.rasterizerDiscardEnable = vk::False;
        rasterizer.polygonMode = Vulkan::toVkPolygonMode(desc.rasterizerState.fillMode);
        rasterizer.cullMode = Vulkan::toVkCullMode(desc.rasterizerState.cullMode);
        rasterizer.frontFace = Vulkan::toVkFrontFace(desc.rasterizerState.frontFace);
        rasterizer.depthBiasEnable = desc.rasterizerState.depthBiasEnable ? vk::True : vk::False;
        rasterizer.depthBiasConstantFactor = desc.rasterizerState.depthBiasConstant;
        rasterizer.depthBiasClamp = desc.rasterizerState.depthBiasClamp;
        rasterizer.depthBiasSlopeFactor = desc.rasterizerState.depthBiasSlopeFactor;
        rasterizer.lineWidth = desc.rasterizerState.lineWidth;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = Vulkan::toVkSampleCount(desc.multisampleState.sampleCount);
        multisampling.sampleShadingEnable = desc.multisampleState.sampleShadingEnable ? vk::True : vk::False;
        multisampling.minSampleShading = desc.multisampleState.minSampleShading;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = desc.depthStencilState.depthTestEnable ? vk::True : vk::False;
        depthStencil.depthWriteEnable = desc.depthStencilState.depthWriteEnable ? vk::True : vk::False;
        depthStencil.depthCompareOp = Vulkan::toVkCompareOp(desc.depthStencilState.depthCompareOp);
        depthStencil.depthBoundsTestEnable = desc.depthStencilState.depthBoundsTestEnable ? vk::True : vk::False;
        depthStencil.minDepthBounds = desc.depthStencilState.minDepthBounds;
        depthStencil.maxDepthBounds = desc.depthStencilState.maxDepthBounds;
        depthStencil.stencilTestEnable = desc.depthStencilState.stencilTestEnable ? vk::True : vk::False;
        depthStencil.front = Vulkan::toVkStencilOpState(
            desc.depthStencilState.front,
            desc.depthStencilState.stencilReadMask,
            desc.depthStencilState.stencilWriteMask,
            desc.depthStencilState.stencilRef);
        depthStencil.back = Vulkan::toVkStencilOpState(
            desc.depthStencilState.back,
            desc.depthStencilState.stencilReadMask,
            desc.depthStencilState.stencilWriteMask,
            desc.depthStencilState.stencilRef);

        std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
        colorBlendAttachments.reserve(desc.blendState.attachmentCount);
        for (uint32_t i = 0; i < desc.blendState.attachmentCount; ++i)
        {
            const auto& att = desc.blendState.attachments[i];
            vk::PipelineColorBlendAttachmentState b{};
            b.blendEnable = att.blendEnable ? vk::True : vk::False;
            b.srcColorBlendFactor = Vulkan::toVkBlendFactor(att.srcColorFactor);
            b.dstColorBlendFactor = Vulkan::toVkBlendFactor(att.dstColorFactor);
            b.colorBlendOp = Vulkan::toVkBlendOp(att.colorBlendOp);
            b.srcAlphaBlendFactor = Vulkan::toVkBlendFactor(att.srcAlphaFactor);
            b.dstAlphaBlendFactor = Vulkan::toVkBlendFactor(att.dstAlphaFactor);
            b.alphaBlendOp = Vulkan::toVkBlendOp(att.alphaBlendOp);
            b.colorWriteMask = Vulkan::toVkColorWriteMask(att.writeMask);
            colorBlendAttachments.push_back(b);
        }

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = desc.blendState.logicOpEnable ? vk::True : vk::False;
        colorBlending.logicOp = Vulkan::toVkLogicOp(desc.blendState.logicOp);
        colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlending.pAttachments = colorBlendAttachments.data();
        std::memcpy(colorBlending.blendConstants.data(), desc.blendState.blendConstants, sizeof(float) * 4);

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &pipelineRenderingCreateInfo;
        pipelineInfo.stageCount = shaderProgram.stageCount();
        pipelineInfo.pStages = shaderProgram.stageInfos().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = *m_pipelineLayout;
        pipelineInfo.renderPass = nullptr;

        m_pipeline = vk::raii::Pipeline(m_device->Get(), nullptr, pipelineInfo);
    }
}

