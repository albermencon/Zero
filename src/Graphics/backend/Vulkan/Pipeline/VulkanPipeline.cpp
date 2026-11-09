#include "pch.h"
#include "Graphics/backend/Vulkan/Pipeline/VulkanPipeline.h"
#include "Graphics/backend/Vulkan/Shader/ShaderModule.h"
#include "Graphics/backend/Vulkan/Shader/ShaderProgram.h"
#include "Graphics/backend/Vulkan/Device/VulkanLogicalDevice.h"
#include "Graphics/backend/Vulkan/Swapchain/VulkanSwapchain.h"
#include "Graphics/backend/Vulkan/Translator/VulkanTranslator.h"

#include <iostream>
#include <stdexcept>
#include <array>
#include <vector>
#include <cstring>

namespace Zero
{
    VulkanPipeline::VulkanPipeline(VulkanLogicalDevice* device, const PipelineDesc& desc)
        : m_device(device), m_swapchain(nullptr)
    {
        if (desc.isCompute())
        {
            createComputePipeline(desc);
        }
        else
        {
            createFromDesc(desc);
        }
    }

    VulkanPipeline::VulkanPipeline(VulkanLogicalDevice* device, VulkanSwapchain* swapchain)
        : m_device(device), m_swapchain(swapchain)
    {
        createLegacyGraphicsPipeline();
    }

    void VulkanPipeline::createFromDesc(const PipelineDesc& desc)
    {
        m_isCompute = false;

        // 1. Shaders
        ShaderProgram shaderProgram;
        std::vector<ShaderModule> ownedModules;

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

        // 2. Dynamic States
        std::vector<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // 3. Descriptor Set Layout & Pipeline Layout
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

        descriptorSetLayout = vk::raii::DescriptorSetLayout(m_device->Get(), descLayoutInfo);
        vk::DescriptorSetLayout rawLayout = *descriptorSetLayout;

        std::vector<vk::PushConstantRange> pushConstants;
        pushConstants.reserve(desc.pushConstants.size());
        for (const auto& pc : desc.pushConstants)
        {
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

        pipelineLayout = vk::raii::PipelineLayout(m_device->Get(), pipelineLayoutInfo);

        // 4. Dynamic Rendering Format Specification
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

        // 5. Vertex Input State
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

        // 6. Input Assembly State
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = Vulkan::toVkTopology(desc.topology);
        inputAssembly.primitiveRestartEnable = desc.primitiveRestart ? VK_TRUE : VK_FALSE;

        // 7. Viewport State (dynamic)
        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // 8. Rasterization State
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

        // 9. Multisampling
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = Vulkan::toVkSampleCount(desc.multisampleState.sampleCount);
        multisampling.sampleShadingEnable = desc.multisampleState.sampleShadingEnable ? vk::True : vk::False;
        multisampling.minSampleShading = desc.multisampleState.minSampleShading;

        // 10. Depth Stencil State
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

        // 11. Color Blending
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

        // 12. Pipeline Creation
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
        pipelineInfo.layout = *pipelineLayout;
        pipelineInfo.renderPass = nullptr;

        graphicsPipeline = vk::raii::Pipeline(m_device->Get(), nullptr, pipelineInfo);
    }

    void VulkanPipeline::createComputePipeline(const PipelineDesc& desc)
    {
        m_isCompute = true;

        // Descriptors & Push Constants
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

        descriptorSetLayout = vk::raii::DescriptorSetLayout(m_device->Get(), descLayoutInfo);
        vk::DescriptorSetLayout rawLayout = *descriptorSetLayout;

        std::vector<vk::PushConstantRange> pushConstants;
        pushConstants.reserve(desc.pushConstants.size());
        for (const auto& pc : desc.pushConstants)
        {
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

        pipelineLayout = vk::raii::PipelineLayout(m_device->Get(), pipelineLayoutInfo);

        // Compute Shader Stage
        auto computeSource = SpirVSource::fromFile(desc.shaders.compute.path);
        auto computeModule = ShaderModule(m_device->Get(), std::move(computeSource));
        const char* ep = desc.shaders.compute.entryPoint ? desc.shaders.compute.entryPoint : "main";

        vk::PipelineShaderStageCreateInfo stageInfo{};
        stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
        stageInfo.module = computeModule.handle();
        stageInfo.pName = ep;

        vk::ComputePipelineCreateInfo computeInfo{};
        computeInfo.stage = stageInfo;
        computeInfo.layout = *pipelineLayout;

        computePipeline = vk::raii::Pipeline(m_device->Get(), nullptr, computeInfo);
    }

    void VulkanPipeline::createLegacyGraphicsPipeline()
    {
        try 
        {
            SpirVSource vertSource = SpirVSource::fromFile("Shaders/shader.vert.spv");
            SpirVSource fragSource = SpirVSource::fromFile("Shaders/shader.frag.spv");

            auto shaderVertModule = ShaderModule(m_device->Get(), std::move(vertSource));
            auto shaderFragModule = ShaderModule(m_device->Get(), std::move(fragSource));

            auto verShaderStage = ShaderStage::vertex(shaderVertModule, "main");
            auto fragShaderStage = ShaderStage::fragment(shaderFragModule, "main");

            ShaderProgram shaderProgram = ShaderProgram();
            shaderProgram.addStage(std::move(verShaderStage));
            shaderProgram.addStage(std::move(fragShaderStage));

            std::vector dynamicStates = {
                vk::DynamicState::eViewport,
                vk::DynamicState::eScissor
            };

            vk::PipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
            dynamicState.pDynamicStates = dynamicStates.data();

            // Set 0: V-LHZ Bitfield (SSBO at binding 0) + LightParams (UBO at binding 1)
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
                vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
                vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment}
            };

            vk::DescriptorSetLayoutCreateInfo descLayoutInfo{};
            descLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            descLayoutInfo.pBindings = bindings.data();

            descriptorSetLayout = vk::raii::DescriptorSetLayout(m_device->Get(), descLayoutInfo);
            vk::DescriptorSetLayout rawLayout = *descriptorSetLayout;

            vk::PushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
            pushConstantRange.offset = 0;
            pushConstantRange.size = 80;

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &rawLayout;
            pipelineLayoutInfo.pushConstantRangeCount = 1;
            pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

            pipelineLayout = vk::raii::PipelineLayout(m_device->Get(), pipelineLayoutInfo);

            vk::Format format = m_swapchain ? m_swapchain->GetFormat() : vk::Format::eB8G8R8A8Unorm;
            vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
            pipelineRenderingCreateInfo.colorAttachmentCount = 1;
            pipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
            pipelineRenderingCreateInfo.depthAttachmentFormat = vk::Format::eD32Sfloat;

            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(uint32_t) * 2;
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;

            vk::VertexInputAttributeDescription attributeDescription{};
            attributeDescription.location = 0;
            attributeDescription.binding = 0;
            attributeDescription.format = vk::Format::eR32G32Uint;
            attributeDescription.offset = 0;

            vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
            vertexInputInfo.vertexAttributeDescriptionCount = 1;
            vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            vk::PipelineViewportStateCreateInfo viewportState{};
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            vk::PipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.depthClampEnable = vk::False;
            rasterizer.rasterizerDiscardEnable = vk::False;
            rasterizer.polygonMode = vk::PolygonMode::eFill;
            rasterizer.cullMode = vk::CullModeFlagBits::eNone;
            rasterizer.frontFace = vk::FrontFace::eClockwise;
            rasterizer.depthBiasEnable = vk::False;
            rasterizer.depthBiasSlopeFactor = 1.0f;
            rasterizer.lineWidth = 1.0f;

            vk::PipelineMultisampleStateCreateInfo multisampling{};
            multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
            multisampling.sampleShadingEnable = vk::False;

            vk::PipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.depthTestEnable = vk::True;
            depthStencil.depthWriteEnable = vk::True;
            depthStencil.depthCompareOp = vk::CompareOp::eLess;
            depthStencil.depthBoundsTestEnable = vk::False;
            depthStencil.stencilTestEnable = vk::False;

            vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.blendEnable = vk::False;
            colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

            vk::PipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.logicOpEnable = vk::False;
            colorBlending.logicOp = vk::LogicOp::eCopy;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

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
            pipelineInfo.layout = *pipelineLayout;
            pipelineInfo.renderPass = nullptr;

            graphicsPipeline = vk::raii::Pipeline(m_device->Get(), nullptr, pipelineInfo);
        }
        catch (const std::exception& e) 
        {
            std::cerr << "[FATAL] Legacy Shader Loading Exception: " << e.what() << '\n';
        }
    }
}
