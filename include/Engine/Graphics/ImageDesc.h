#pragma once
#include <cstdint>
#include "GraphicsTypes.h"
#include "MemoryDomain.h"

namespace Zero
{
    // =========================================================================
    // ImageUsageFlags
    //
    //   Bitmask declaring every way an image will be used over its lifetime.
    //   The backend uses this to:
    //     - Vulkan : VkImageUsageFlags
    //     - DX12   : D3D12_RESOURCE_FLAGS + heap type hints
    //     - Metal  : MTLTextureUsage
    //     - OpenGL : texture storage hints / image unit bindings
    //
    //   RULES:
    //     - Declare ALL intended usages upfront at creation time.
    //     - Never set flags you won't use (wastes memory bandwidth / disables HW compression).
    //     - CopySrc / CopyDst are needed for any staging / readback transfers.
    //     - UnorderedAccess requires a UAV-compatible format.
    //     - ShadingRate requires VK_KHR_fragment_shading_rate / DX12 tier support.
    // =========================================================================
    enum class ImageUsage : uint32_t
    {
        None = 0,

        // ---- Sampling / reading in shaders ----------------------------------
        Sampled = 1 << 0,   // Texture2D / texture() in shader
        Storage = 1 << 1,   // RWTexture / image load-store (UAV)

        // ---- Render target attachment roles ---------------------------------
        ColorAttachment = 1 << 2,   // bound as color render target
        DepthAttachment = 1 << 3,   // depth component of depth-stencil
        StencilAttachment = 1 << 4,   // stencil component of depth-stencil
        InputAttachment = 1 << 5,   // Vulkan subpass input (read previous pass)

        // ---- Transfer / copy ------------------------------------------------
        CopySrc = 1 << 6,   // source of copy / blit / download
        CopyDst = 1 << 7,   // destination of copy / upload / clear

        // ---- Indirect / specialized -----------------------------------------
        ShadingRate = 1 << 8,   // VRS / shading-rate image
        TransientAttachment = 1 << 9,   // memoryless / tile-local (Vulkan / Metal)
        // never leaves the tile; no backing VRAM needed

// ---- Convenience composites -----------------------------------------
DepthStencilAttachment = DepthAttachment | StencilAttachment,

RenderTexture = ColorAttachment | Sampled,           // classic RTT
DepthBuffer = DepthAttachment | Sampled,           // shadow map, SSAO input
StorageTexture = Storage | Sampled,           // compute read+write
SwapchainImage = ColorAttachment | CopySrc,           // typical swapchain
    };

    inline constexpr ImageUsage operator|(ImageUsage a, ImageUsage b)
    {
        return static_cast<ImageUsage>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline constexpr ImageUsage operator&(ImageUsage a, ImageUsage b)
    {
        return static_cast<ImageUsage>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    inline constexpr ImageUsage operator~(ImageUsage a)
    {
        return static_cast<ImageUsage>(~static_cast<uint32_t>(a));
    }
    inline constexpr bool hasUsage(ImageUsage flags, ImageUsage query)
    {
        return (flags & query) != ImageUsage::None;
    }

    // =========================================================================
    // ImageLayout  (aka resource state)
    //
    //   Describes the *current* memory layout / access pattern of the image.
    //   The engine's barrier system transitions between these states.
    //   Maps to:
    //     Vulkan  : VkImageLayout
    //     DX12    : D3D12_RESOURCE_STATES
    //     Metal   : implicit (Metal tracks via encoder types)
    //     OpenGL  : layout qualifiers / glMemoryBarrier
    // =========================================================================
    enum class ImageLayout : uint8_t
    {
        Undefined,              // don't-care; contents may be discarded on transition

        General,                // catch-all (Vulkan: VK_IMAGE_LAYOUT_GENERAL)
        // slower than specific layouts — avoid if possible

        ColorAttachment,        // writing to as a render target color output
        DepthStencilAttachment, // writing to as depth/stencil output (read+write)
        DepthStencilReadOnly,   // depth read + sampled (shadow map sampling while bound)

        ShaderReadOnly,         // sampled / fetched in any shader stage
        StorageReadWrite,       // image load/store (compute UAV)
        StorageReadOnly,        // image load only (read-only UAV)

        CopySrc,                // source of vkCmdCopyImage / CopyResource
        CopyDst,                // destination of copy / clear

        Present,                // ready to be presented by swapchain
        ShadingRate,            // used as VRS shading-rate image
    };

    // =========================================================================
    // ImageType
    //   Dimensionality. Array and cube variants are flags on ImageDesc.
    // =========================================================================
    enum class ImageType : uint8_t
    {
        Image1D,
        Image2D,        // the common case
        Image3D,        // volume texture
    };

    // =========================================================================
    // ImageTiling / Memory Domain
    //
    //   Vulkan explicitly exposes tiling; DX12/Metal infer it from heap type.
    //   Prefer Optimal for GPU-only images.
    //   Linear is only for staging / host-read images.
    // =========================================================================
    enum class ImageTiling : uint8_t
    {
        Optimal,    // GPU-native swizzled layout (fastest for sampling/attachment)
        Linear,     // row-major; required for host mapping / readback
    };

    // =========================================================================
    // ImageAspect  (which sub-resource planes are targeted)
    //   Used in image views and barriers.
    // =========================================================================
    enum class ImageAspect : uint8_t
    {
        Color,
        Depth,
        Stencil,
        DepthStencil,   // both planes (Vulkan only in some barriers)
        Plane0,         // YCbCr plane 0 (Y)
        Plane1,         // YCbCr plane 1 (Cb / U)
        Plane2,         // YCbCr plane 2 (Cr / V)
    };

    // =========================================================================
    // ImageViewType
    //   What shape the shader sees, which may differ from the base image type.
    //   e.g. a 2D array image can be viewed as a CubeMap.
    // =========================================================================
    enum class ImageViewType : uint8_t
    {
        View1D,
        View1DArray,
        View2D,
        View2DArray,
        ViewCube,
        ViewCubeArray,
        View3D,
    };

    // =========================================================================
    // ImageDesc
    //
    //   Everything the engine needs to allocate and initialize an image.
    //   Pass this to renderer.createImage(desc).
    //
    //   Examples:
    //
    //   // G-Buffer albedo RT
    //   ImageDesc albedo;
    //   albedo.debugName = "GBuffer_Albedo";
    //   albedo.width     = 1920; albedo.height = 1080;
    //   albedo.format    = PixelFormat::RGBA8_UNorm;
    //   albedo.usage     = ImageUsage::RenderTexture;        // ColorAttachment | Sampled
    //
    //   // Shadow map
    //   ImageDesc shadow = ImageDesc::DepthTarget(2048, 2048, PixelFormat::D32_Float);
    //   shadow.debugName = "ShadowMap";
    //   shadow.usage     |= ImageUsage::Sampled;             // read in lighting pass
    //
    //   // Compute output UAV
    //   ImageDesc hdr = ImageDesc::StorageTarget(1920, 1080, PixelFormat::RGBA16_Float);
    //
    //   // Cubemap from pre-baked assets
    //   ImageDesc sky = ImageDesc::Cubemap(512, PixelFormat::RGBA16_Float, 9 /*mips*/);
    // =========================================================================
    struct ImageDesc
    {
        // -- Identity ---------------------------------------------------------
        const char* debugName = nullptr;

        // -- Dimensions -------------------------------------------------------
        uint32_t     width = 1;
        uint32_t     height = 1;
        uint32_t     depth = 1;       // > 1 for Image3D only
        uint32_t     mipLevels = 1;       // 1 = no mips; 0 = compute full chain
        uint32_t     arrayLayers = 1;       // > 1 for arrays, == 6 for cube, etc.

        // -- Type & Format ----------------------------------------------------
        ImageType    type = ImageType::Image2D;
        PixelFormat  format = PixelFormat::RGBA8_UNorm;
        SampleCount  samples = SampleCount::Count1;

        // -- Usage & Memory ---------------------------------------------------
        ImageUsage   usage = ImageUsage::Sampled;
        ImageTiling  tiling = ImageTiling::Optimal;
        MemoryDomain memory = MemoryDomain::GPU;

        // -- Layout at creation -----------------------------------------------
        //    Most images start Undefined (contents discarded) and are
        //    transitioned before first use.  Set to a specific layout only
        //    if you're uploading data immediately at creation.
        ImageLayout  initialLayout = ImageLayout::Undefined;

        // -- Cube / array flags -----------------------------------------------
        bool isCubeCompatible = false;   // allows cube-map views (arrayLayers must be % 6)

        // =====================================================================
        // Factory helpers
        // =====================================================================

        /// Plain 2D color texture (sampled only — for uploaded assets)
        [[nodiscard]] static constexpr ImageDesc Texture2D(
            uint32_t w, uint32_t h,
            PixelFormat fmt = PixelFormat::RGBA8_UNorm,
            uint32_t mips = 1)
        {
            ImageDesc d;
            d.width = w;
            d.height = h;
            d.mipLevels = mips;
            d.format = fmt;
            d.usage = ImageUsage::Sampled | ImageUsage::CopyDst;
            return d;
        }

        /// Color render target (can also be sampled in subsequent passes)
        [[nodiscard]] static constexpr ImageDesc ColorTarget(
            uint32_t w, uint32_t h,
            PixelFormat fmt = PixelFormat::RGBA16_Float,
            SampleCount samples = SampleCount::Count1)
        {
            ImageDesc d;
            d.width = w;
            d.height = h;
            d.format = fmt;
            d.samples = samples;
            d.usage = ImageUsage::RenderTexture;   // ColorAttachment | Sampled
            return d;
        }

        /// Depth (+ optional stencil) render target
        [[nodiscard]] static constexpr ImageDesc DepthTarget(
            uint32_t w, uint32_t h,
            PixelFormat fmt = PixelFormat::D32_Float,
            SampleCount samples = SampleCount::Count1)
        {
            ImageDesc d;
            d.width = w;
            d.height = h;
            d.format = fmt;
            d.samples = samples;
            d.usage = ImageUsage::DepthBuffer;     // DepthAttachment | Sampled
            return d;
        }

        /// Read-write compute image (UAV / storage image)
        [[nodiscard]] static constexpr ImageDesc StorageTarget(
            uint32_t w, uint32_t h,
            PixelFormat fmt = PixelFormat::RGBA16_Float)
        {
            ImageDesc d;
            d.width = w;
            d.height = h;
            d.format = fmt;
            d.usage = ImageUsage::StorageTexture;   // Storage | Sampled
            return d;
        }

        /// Cube-map (6-layer 2D array)
        [[nodiscard]] static constexpr ImageDesc Cubemap(
            uint32_t faceSize,
            PixelFormat fmt = PixelFormat::RGBA16_Float,
            uint32_t mips = 1)
        {
            ImageDesc d;
            d.width = faceSize;
            d.height = faceSize;
            d.mipLevels = mips;
            d.arrayLayers = 6;
            d.format = fmt;
            d.isCubeCompatible = true;
            d.usage = ImageUsage::Sampled | ImageUsage::CopyDst;
            return d;
        }

        /// Memoryless MSAA resolve target (tiled / on-chip only)
        [[nodiscard]] static constexpr ImageDesc Transient(
            uint32_t w, uint32_t h,
            PixelFormat fmt,
            SampleCount samples = SampleCount::Count4)
        {
            ImageDesc d;
            d.width = w;
            d.height = h;
            d.format = fmt;
            d.samples = samples;
            d.usage = ImageUsage::ColorAttachment | ImageUsage::TransientAttachment;
            d.memory = MemoryDomain::GPU;
            return d;
        }

        /// Volume / 3D texture
        [[nodiscard]] static constexpr ImageDesc Volume(
            uint32_t w, uint32_t h, uint32_t d,
            PixelFormat fmt = PixelFormat::RGBA8_UNorm,
            uint32_t mips = 1)
        {
            ImageDesc desc;
            desc.type = ImageType::Image3D;
            desc.width = w;
            desc.height = h;
            desc.depth = d;
            desc.mipLevels = mips;
            desc.format = fmt;
            desc.usage = ImageUsage::Sampled | ImageUsage::CopyDst;
            return desc;
        }

        // -- Helpers ----------------------------------------------------------

        [[nodiscard]] constexpr bool isDepthFormat()   const
        {
            return format == PixelFormat::D16_UNorm
                || format == PixelFormat::D32_Float
                || format == PixelFormat::D24_UNorm_S8_UInt
                || format == PixelFormat::D32_Float_S8_UInt;
        }

        [[nodiscard]] constexpr bool hasStencil() const
        {
            return format == PixelFormat::D24_UNorm_S8_UInt
                || format == PixelFormat::D32_Float_S8_UInt;
        }

        [[nodiscard]] constexpr bool isMultisampled() const
        {
            return samples != SampleCount::Count1;
        }

        [[nodiscard]] constexpr bool hasMips() const
        {
            return mipLevels != 1;
        }

        [[nodiscard]] constexpr ImageAspect defaultAspect() const
        {
            if (hasStencil())  return ImageAspect::DepthStencil;
            if (isDepthFormat()) return ImageAspect::Depth;
            return ImageAspect::Color;
        }
    };

    // ImageViewDesc
    //
    //   Describes a *view* into a base image — a specific mip range,
    //   layer range, or reinterpreted format/type.
    //   The engine creates these internally, but exposing them lets
    //   clients bind individual mip levels or array slices explicitly.
    struct ImageViewDesc
    {
        ImageViewType viewType = ImageViewType::View2D;
        PixelFormat   format = PixelFormat::Undefined;  // Undefined = same as image
        ImageAspect   aspect = ImageAspect::Color;

        uint32_t      baseMipLevel = 0;
        uint32_t      mipLevelCount = 0;        // 0 = all remaining mips
        uint32_t      baseArrayLayer = 0;
        uint32_t      arrayLayerCount = 0;      // 0 = all remaining layers
    };

}
