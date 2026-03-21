#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <Engine/Core.h>
#include "GraphicsTypes.h"
#include "VertexLayout.h"

namespace Zero 
{
	static constexpr uint32_t MaxColorAttachments = 8;
	static constexpr uint32_t MaxShaderStages = 6;  // VS, HS, DS, GS, PS, CS

    struct ShaderDesc
    {
        const char* path = nullptr;   // asset path, e.g. "Shaders/PBR.hlsl"
        const char* entryPoint = "main";
    };

    // Describes all active shader stages for the pipeline.
    // Unused stages are left as default (nullptr path).
    struct ShaderProgramDesc
    {
        ShaderDesc vertex;
        ShaderDesc fragment;

        // Optional stages
        ShaderDesc hull;                 // tessellation control  (DX12: HS, Vulkan: TCS)
        ShaderDesc domain;               // tessellation eval     (DX12: DS, Vulkan: TES)
        ShaderDesc geometry;             // geometry shader

        [[nodiscard]] constexpr bool hasVertex()   const { return vertex.path != nullptr; }
        [[nodiscard]] constexpr bool hasFragment()  const { return fragment.path != nullptr; }
        [[nodiscard]] constexpr bool hasHull()      const { return hull.path != nullptr; }
        [[nodiscard]] constexpr bool hasDomain()    const { return domain.path != nullptr; }
        [[nodiscard]] constexpr bool hasGeometry()  const { return geometry.path != nullptr; }
    };

    struct RasterizerState
    {
        FillMode fillMode = FillMode::Solid;
        CullMode cullMode = CullMode::Back;
        FrontFace frontFace = FrontFace::CounterClockwise;
        bool     depthClampEnable = false;   // clamp depth instead of clip
        bool     depthBiasEnable = false;
        float    depthBiasConstant = 0.0f;    // constant added to depth
        float    depthBiasClamp = 0.0f;    // max / min clamping value
        float    depthBiasSlopeFactor = 0.0f;    // slope-scaled bias
        float    lineWidth = 1.0f;    // only Vulkan; DX12 ignores this
        bool     conservativeRaster = false;   // conservative rasterization extension

        // Factory helpers

        [[nodiscard]] static constexpr RasterizerState Default()
        {
            return {};
        }

        [[nodiscard]] static constexpr RasterizerState Wireframe()
        {
            RasterizerState s;
            s.fillMode = FillMode::Wireframe;
            s.cullMode = CullMode::None;
            return s;
        }

        [[nodiscard]] static constexpr RasterizerState ShadowMap()
        {
            RasterizerState s;
            s.cullMode = CullMode::Front;  // Peter-Pan fix
            s.depthBiasEnable = true;
            s.depthBiasConstant = 2.0f;
            s.depthBiasSlopeFactor = 2.5f;
            return s;
        }

        [[nodiscard]] static constexpr RasterizerState NoCull()
        {
            RasterizerState s;
            s.cullMode = CullMode::None;
            return s;
        }
    };

    // Specifies what happens for front or back faces during stencil tests.
    struct StencilOpState
    {
        StencilOp failOp = StencilOp::Keep;    // stencil test fails
        StencilOp depthFailOp = StencilOp::Keep;    // stencil passes, depth fails
        StencilOp passOp = StencilOp::Keep;    // both pass
        CompareOp compareOp = CompareOp::Always;
    };

    struct DepthStencilState
    {
        // Depth
        bool      depthTestEnable = true;
        bool      depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::Less;

        // Depth bounds (Vulkan / DX12 extension)
        bool  depthBoundsTestEnable = false;
        float minDepthBounds = 0.0f;
        float maxDepthBounds = 1.0f;

        // Stencil
        bool          stencilTestEnable = false;
        uint8_t       stencilReadMask = 0xFF;
        uint8_t       stencilWriteMask = 0xFF;
        uint8_t       stencilRef = 0;       // dynamic in Vulkan; state in DX12
        StencilOpState front{};
        StencilOpState back{};

        // Factory helpers

        [[nodiscard]] static constexpr DepthStencilState Default()
        {
            return {};
        }

        [[nodiscard]] static constexpr DepthStencilState ReadOnly()
        {
            DepthStencilState s;
            s.depthWriteEnable = false;
            return s;
        }

        [[nodiscard]] static constexpr DepthStencilState Disabled()
        {
            DepthStencilState s;
            s.depthTestEnable = false;
            s.depthWriteEnable = false;
            return s;
        }

        [[nodiscard]] static constexpr DepthStencilState DepthEqual()
        {
            DepthStencilState s;
            s.depthWriteEnable = false;
            s.depthCompareOp = CompareOp::Equal;
            return s;
        }
    };

    // Per-render-target blend configuration.
    struct BlendAttachmentState
    {
        bool            blendEnable = false;
        BlendFactor     srcColorFactor = BlendFactor::One;
        BlendFactor     dstColorFactor = BlendFactor::Zero;
        BlendOp         colorBlendOp = BlendOp::Add;
        BlendFactor     srcAlphaFactor = BlendFactor::One;
        BlendFactor     dstAlphaFactor = BlendFactor::Zero;
        BlendOp         alphaBlendOp = BlendOp::Add;
        ColorWriteMask  writeMask = ColorWriteMask::All;

        // Factory helpers

        [[nodiscard]] static constexpr BlendAttachmentState Opaque()
        {
            return {};
        }

        [[nodiscard]] static constexpr BlendAttachmentState AlphaBlend()
        {
            BlendAttachmentState s;
            s.blendEnable = true;
            s.srcColorFactor = BlendFactor::SrcAlpha;
            s.dstColorFactor = BlendFactor::OneMinusSrcAlpha;
            s.srcAlphaFactor = BlendFactor::One;
            s.dstAlphaFactor = BlendFactor::Zero;
            return s;
        }

        [[nodiscard]] static constexpr BlendAttachmentState Additive()
        {
            BlendAttachmentState s;
            s.blendEnable = true;
            s.srcColorFactor = BlendFactor::One;
            s.dstColorFactor = BlendFactor::One;
            s.srcAlphaFactor = BlendFactor::One;
            s.dstAlphaFactor = BlendFactor::One;
            return s;
        }

        [[nodiscard]] static constexpr BlendAttachmentState Premultiplied()
        {
            BlendAttachmentState s;
            s.blendEnable = true;
            s.srcColorFactor = BlendFactor::One;
            s.dstColorFactor = BlendFactor::OneMinusSrcAlpha;
            s.srcAlphaFactor = BlendFactor::One;
            s.dstAlphaFactor = BlendFactor::OneMinusSrcAlpha;
            return s;
        }

        [[nodiscard]] static constexpr BlendAttachmentState NoWrite()
        {
            BlendAttachmentState s;
            s.writeMask = ColorWriteMask::None;
            return s;
        }
    };

    // Global blend configuration covering up to MaxColorAttachments targets.
    struct BlendState
    {
        bool    logicOpEnable = false;
        LogicOp logicOp = LogicOp::Copy;
        float   blendConstants[4] = { 0, 0, 0, 0 };
        bool    alphaToCoverageEnable = false;   // MSAA alpha-to-coverage
        bool    independentBlendEnable = false;   // use per-RT blend states
        uint32_t attachmentCount = 1;
        std::array<BlendAttachmentState, MaxColorAttachments> attachments{};

        constexpr BlendState()
        {
            // All attachments default to opaque
            attachments.fill(BlendAttachmentState::Opaque());
        }

        // Factory helpers

        [[nodiscard]] static BlendState Opaque()
        {
            return {};
        }

        [[nodiscard]] static BlendState AlphaBlend(uint32_t numAttachments = 1)
        {
            BlendState s;
            s.attachmentCount = numAttachments;
            for (uint32_t i = 0; i < numAttachments; ++i)
                s.attachments[i] = BlendAttachmentState::AlphaBlend();
            return s;
        }

        [[nodiscard]] static BlendState Additive(uint32_t numAttachments = 1)
        {
            BlendState s;
            s.attachmentCount = numAttachments;
            for (uint32_t i = 0; i < numAttachments; ++i)
                s.attachments[i] = BlendAttachmentState::Additive();
            return s;
        }
    };

    // MultisampleState
    struct MultisampleState
    {
        SampleCount sampleCount = SampleCount::Count1;
        bool        sampleShadingEnable = false;
        float       minSampleShading = 1.0f;    // [0,1] fraction of samples shaded
        uint32_t    sampleMask = ~0u;
    };

    // RenderTargetLayout
    //   Tells the pipeline which color/depth formats to expect.
    //   Replaces VkRenderPass / DX12 DXGI_FORMAT in the PSO.
    struct RenderTargetLayout
    {
        std::array<PixelFormat, MaxColorAttachments> colorFormats{};
        uint32_t  colorCount = 1;
        PixelFormat depthStencilFormat = PixelFormat::D32_Float;

        constexpr RenderTargetLayout()
        {
            colorFormats.fill(PixelFormat::Undefined);
            colorFormats[0] = PixelFormat::BGRA8_UNorm;     // sensible default swapchain
        }

        constexpr RenderTargetLayout(PixelFormat color,
            PixelFormat depth = PixelFormat::D32_Float)
        {
            colorFormats.fill(PixelFormat::Undefined);
            colorFormats[0] = color;
            colorCount = 1;
            depthStencilFormat = depth;
        }
    };

    //   PipelineDesc desc;
    //   desc.debugName       = "PBR Forward";
    //   desc.shaders.vertex  = { "Shaders/PBR.vert.spv" };
    //   desc.shaders.fragment = { "Shaders/PBR.frag.spv" };
    //   desc.vertexLayout    = Layouts::Pos3_Norm3_UV2;
    //   desc.blendState      = BlendState::Opaque();
    //   auto handle = renderer.createPipeline(desc);
    struct PipelineDesc
    {
        // Identity
        const char* debugName = nullptr;       // captured by Vulkan debug utils, PIX, etc.

        // haders
        ShaderProgramDesc shaders;

        // Input Assembly
        VertexLayout     vertexLayout;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        bool              primitiveRestart = false;  // only valid for Strip topologies

        // Tessellation
        uint32_t patchControlPoints = 0;       // > 0 only when hull/domain shaders are set

        // Render States
        RasterizerState   rasterizerState;
        DepthStencilState depthStencilState;
        BlendState        blendState;
        MultisampleState  multisampleState;

        // Must match the actual attachments used at draw time.
        RenderTargetLayout renderTargetLayout;

        // Convenience factories — common pipeline archetypes

        /// Standard opaque forward-rendering pass
        [[nodiscard]] static PipelineDesc ForwardOpaque(
            ShaderProgramDesc shaders,
            const VertexLayout& layout = Layouts::Pos3_Norm3_UV2)
        {
            PipelineDesc d;
            d.shaders = shaders;
            d.vertexLayout = layout;
            d.rasterizerState = RasterizerState::Default();
            d.depthStencilState = DepthStencilState::Default();
            d.blendState = BlendState::Opaque();
            return d;
        }

        /// Transparent pass (alpha blended, depth read-only)
        [[nodiscard]] static PipelineDesc Transparent(
            ShaderProgramDesc shaders,
            const VertexLayout& layout = Layouts::Pos3_Norm3_UV2)
        {
            PipelineDesc d;
            d.shaders = shaders;
            d.vertexLayout = layout;
            d.rasterizerState = RasterizerState::NoCull();
            d.depthStencilState = DepthStencilState::ReadOnly();
            d.blendState = BlendState::AlphaBlend();
            return d;
        }

        /// Depth pre-pass / shadow map
        [[nodiscard]] static PipelineDesc DepthOnly(
            ShaderProgramDesc shaders,
            const VertexLayout& layout = Layouts::Pos3)
        {
            PipelineDesc d;
            d.shaders = shaders;
            d.vertexLayout = layout;
            d.rasterizerState = RasterizerState::ShadowMap();
            d.depthStencilState = DepthStencilState::Default();
            // No color writes
            BlendState bs;
            bs.attachments[0] = BlendAttachmentState::NoWrite();
            d.blendState = bs;
            d.renderTargetLayout.colorCount = 0;
            d.renderTargetLayout.depthStencilFormat = PixelFormat::D32_Float;
            return d;
        }

        /// Fullscreen / post-process quad (no vertex input, no depth)
        [[nodiscard]] static PipelineDesc FullscreenQuad(ShaderProgramDesc shaders)
        {
            PipelineDesc d;
            d.shaders = shaders;
            // No vertex input — VS generates clip-space triangle from gl_VertexIndex
            d.depthStencilState = DepthStencilState::Disabled();
            d.rasterizerState = RasterizerState::NoCull();
            d.blendState = BlendState::Opaque();
            return d;
        }
    };
}
