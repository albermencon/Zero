#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <Engine/Graphics/GraphicsTypes.h>
#include <Engine/Graphics/ImageDesc.h>
#include <Engine/Graphics/VertexLayout.h>

namespace Zero::Vulkan
{

    [[nodiscard]] constexpr vk::Format toVkFormat(PixelFormat fmt)
    {
        switch (fmt)
        {
        case PixelFormat::Undefined:            return vk::Format::eUndefined;
            // 8-bit
        case PixelFormat::R8_UNorm:             return vk::Format::eR8Unorm;
        case PixelFormat::R8_SNorm:             return vk::Format::eR8Snorm;
        case PixelFormat::R8_UInt:              return vk::Format::eR8Uint;
        case PixelFormat::R8_SInt:              return vk::Format::eR8Sint;
            // 16-bit
        case PixelFormat::RG8_UNorm:            return vk::Format::eR8G8Unorm;
        case PixelFormat::RG8_SNorm:            return vk::Format::eR8G8Snorm;
        case PixelFormat::R16_Float:            return vk::Format::eR16Sfloat;
        case PixelFormat::R16_UInt:             return vk::Format::eR16Uint;
        case PixelFormat::R16_SInt:             return vk::Format::eR16Sint;
            // 32-bit
        case PixelFormat::RGBA8_UNorm:          return vk::Format::eR8G8B8A8Unorm;
        case PixelFormat::RGBA8_UNorm_sRGB:     return vk::Format::eR8G8B8A8Srgb;
        case PixelFormat::RGBA8_SNorm:          return vk::Format::eR8G8B8A8Snorm;
        case PixelFormat::RGBA8_UInt:           return vk::Format::eR8G8B8A8Uint;
        case PixelFormat::RGBA8_SInt:           return vk::Format::eR8G8B8A8Sint;
        case PixelFormat::BGRA8_UNorm:          return vk::Format::eB8G8R8A8Unorm;
        case PixelFormat::BGRA8_UNorm_sRGB:     return vk::Format::eB8G8R8A8Srgb;
        case PixelFormat::RGB10A2_UNorm:        return vk::Format::eA2R10G10B10UnormPack32;
        case PixelFormat::RG11B10_Float:        return vk::Format::eB10G11R11UfloatPack32;
        case PixelFormat::R32_Float:            return vk::Format::eR32Sfloat;
        case PixelFormat::R32_UInt:             return vk::Format::eR32Uint;
        case PixelFormat::R32_SInt:             return vk::Format::eR32Sint;
            // 64-bit
        case PixelFormat::RG32_Float:           return vk::Format::eR32G32Sfloat;
        case PixelFormat::RG32_UInt:            return vk::Format::eR32G32Uint;
        case PixelFormat::RG32_SInt:            return vk::Format::eR32G32Sint;
        case PixelFormat::RGBA16_Float:         return vk::Format::eR16G16B16A16Sfloat;
        case PixelFormat::RGBA16_UNorm:         return vk::Format::eR16G16B16A16Unorm;
            // 128-bit
        case PixelFormat::RGBA32_Float:         return vk::Format::eR32G32B32A32Sfloat;
        case PixelFormat::RGBA32_UInt:          return vk::Format::eR32G32B32A32Uint;
        case PixelFormat::RGBA32_SInt:          return vk::Format::eR32G32B32A32Sint;
            // Depth/Stencil
        case PixelFormat::D16_UNorm:            return vk::Format::eD16Unorm;
        case PixelFormat::D24_UNorm_S8_UInt:    return vk::Format::eD24UnormS8Uint;
        case PixelFormat::D32_Float:            return vk::Format::eD32Sfloat;
        case PixelFormat::D32_Float_S8_UInt:    return vk::Format::eD32SfloatS8Uint;
        }
        return vk::Format::eUndefined;
    }

    [[nodiscard]] constexpr vk::Format toVkFormat(VertexFormat fmt)
    {
        switch (fmt)
        {
        case VertexFormat::Float1:       return vk::Format::eR32Sfloat;
        case VertexFormat::Float2:       return vk::Format::eR32G32Sfloat;
        case VertexFormat::Float3:       return vk::Format::eR32G32B32Sfloat;
        case VertexFormat::Float4:       return vk::Format::eR32G32B32A32Sfloat;
        case VertexFormat::Half2:        return vk::Format::eR16G16Sfloat;
        case VertexFormat::Half4:        return vk::Format::eR16G16B16A16Sfloat;
        case VertexFormat::UByte4_Norm:  return vk::Format::eR8G8B8A8Unorm;
        case VertexFormat::UShort2_Norm: return vk::Format::eR16G16Unorm;
        case VertexFormat::UShort4_Norm: return vk::Format::eR16G16B16A16Unorm;
        case VertexFormat::Byte4_Norm:   return vk::Format::eR8G8B8A8Snorm;
        case VertexFormat::Short2_Norm:  return vk::Format::eR16G16Snorm;
        case VertexFormat::Short4_Norm:  return vk::Format::eR16G16B16A16Snorm;
        case VertexFormat::UByte4:       return vk::Format::eR8G8B8A8Uint;
        case VertexFormat::UInt1:        return vk::Format::eR32Uint;
        case VertexFormat::UInt2:        return vk::Format::eR32G32Uint;
        case VertexFormat::UInt3:        return vk::Format::eR32G32B32Uint;
        case VertexFormat::UInt4:        return vk::Format::eR32G32B32A32Uint;
        case VertexFormat::Int1:         return vk::Format::eR32Sint;
        case VertexFormat::Int2:         return vk::Format::eR32G32Sint;
        case VertexFormat::Int3:         return vk::Format::eR32G32B32Sint;
        case VertexFormat::Int4:         return vk::Format::eR32G32B32A32Sint;
        }
        return vk::Format::eUndefined;
    }

    [[nodiscard]] constexpr vk::VertexInputRate toVkInputRate(VertexInputRate rate)
    {
        return rate == VertexInputRate::PerInstance
            ? vk::VertexInputRate::eInstance
            : vk::VertexInputRate::eVertex;
    }

    [[nodiscard]] constexpr vk::PrimitiveTopology toVkTopology(PrimitiveTopology t)
    {
        switch (t)
        {
        case PrimitiveTopology::PointList:     return vk::PrimitiveTopology::ePointList;
        case PrimitiveTopology::LineList:      return vk::PrimitiveTopology::eLineList;
        case PrimitiveTopology::LineStrip:     return vk::PrimitiveTopology::eLineStrip;
        case PrimitiveTopology::TriangleList:  return vk::PrimitiveTopology::eTriangleList;
        case PrimitiveTopology::TriangleStrip: return vk::PrimitiveTopology::eTriangleStrip;
        case PrimitiveTopology::PatchList:     return vk::PrimitiveTopology::ePatchList;
        }
        return vk::PrimitiveTopology::eTriangleList;
    }

    [[nodiscard]] constexpr vk::PolygonMode toVkPolygonMode(FillMode m)
    {
        switch (m)
        {
        case FillMode::Solid:      return vk::PolygonMode::eFill;
        case FillMode::Wireframe:  return vk::PolygonMode::eLine;
        case FillMode::Point:      return vk::PolygonMode::ePoint;
        }
        return vk::PolygonMode::eFill;
    }

    [[nodiscard]] constexpr vk::CullModeFlags toVkCullMode(CullMode m)
    {
        switch (m)
        {
        case CullMode::None:         return vk::CullModeFlagBits::eNone;
        case CullMode::Front:        return vk::CullModeFlagBits::eFront;
        case CullMode::Back:         return vk::CullModeFlagBits::eBack;
        case CullMode::FrontAndBack: return vk::CullModeFlagBits::eFrontAndBack;
        }
        return vk::CullModeFlagBits::eBack;
    }

    [[nodiscard]] constexpr vk::FrontFace toVkFrontFace(FrontFace f)
    {
        return f == FrontFace::Clockwise
            ? vk::FrontFace::eClockwise
            : vk::FrontFace::eCounterClockwise;
    }

    [[nodiscard]] constexpr vk::CompareOp toVkCompareOp(CompareOp op)
    {
        switch (op)
        {
        case CompareOp::Never:        return vk::CompareOp::eNever;
        case CompareOp::Less:         return vk::CompareOp::eLess;
        case CompareOp::Equal:        return vk::CompareOp::eEqual;
        case CompareOp::LessEqual:    return vk::CompareOp::eLessOrEqual;
        case CompareOp::Greater:      return vk::CompareOp::eGreater;
        case CompareOp::NotEqual:     return vk::CompareOp::eNotEqual;
        case CompareOp::GreaterEqual: return vk::CompareOp::eGreaterOrEqual;
        case CompareOp::Always:       return vk::CompareOp::eAlways;
        }
        return vk::CompareOp::eLess;
    }

    [[nodiscard]] constexpr vk::StencilOp toVkStencilOp(StencilOp op)
    {
        switch (op)
        {
        case StencilOp::Keep:           return vk::StencilOp::eKeep;
        case StencilOp::Zero:           return vk::StencilOp::eZero;
        case StencilOp::Replace:        return vk::StencilOp::eReplace;
        case StencilOp::IncrementClamp: return vk::StencilOp::eIncrementAndClamp;
        case StencilOp::DecrementClamp: return vk::StencilOp::eDecrementAndClamp;
        case StencilOp::Invert:         return vk::StencilOp::eInvert;
        case StencilOp::IncrementWrap:  return vk::StencilOp::eIncrementAndWrap;
        case StencilOp::DecrementWrap:  return vk::StencilOp::eDecrementAndWrap;
        }
        return vk::StencilOp::eKeep;
    }

    [[nodiscard]] constexpr vk::StencilOpState toVkStencilOpState(
        const StencilOpState& s, uint32_t readMask, uint32_t writeMask, uint32_t ref)
    {
        vk::StencilOpState out{};
        out.failOp = toVkStencilOp(s.failOp);
        out.depthFailOp = toVkStencilOp(s.depthFailOp);
        out.passOp = toVkStencilOp(s.passOp);
        out.compareOp = toVkCompareOp(s.compareOp);
        out.compareMask = readMask;
        out.writeMask = writeMask;
        out.reference = ref;
        return out;
    }

    [[nodiscard]] constexpr vk::BlendFactor toVkBlendFactor(BlendFactor f)
    {
        switch (f)
        {
        case BlendFactor::Zero:                return vk::BlendFactor::eZero;
        case BlendFactor::One:                 return vk::BlendFactor::eOne;
        case BlendFactor::SrcColor:            return vk::BlendFactor::eSrcColor;
        case BlendFactor::OneMinusSrcColor:    return vk::BlendFactor::eOneMinusSrcColor;
        case BlendFactor::DstColor:            return vk::BlendFactor::eDstColor;
        case BlendFactor::OneMinusDstColor:    return vk::BlendFactor::eOneMinusDstColor;
        case BlendFactor::SrcAlpha:            return vk::BlendFactor::eSrcAlpha;
        case BlendFactor::OneMinusSrcAlpha:    return vk::BlendFactor::eOneMinusSrcAlpha;
        case BlendFactor::DstAlpha:            return vk::BlendFactor::eDstAlpha;
        case BlendFactor::OneMinusDstAlpha:    return vk::BlendFactor::eOneMinusDstAlpha;
        case BlendFactor::ConstantColor:       return vk::BlendFactor::eConstantColor;
        case BlendFactor::OneMinusConstantColor:return vk::BlendFactor::eOneMinusConstantColor;
        case BlendFactor::SrcAlphaSaturate:    return vk::BlendFactor::eSrcAlphaSaturate;
        case BlendFactor::Src1Color:           return vk::BlendFactor::eSrc1Color;
        case BlendFactor::OneMinusSrc1Color:   return vk::BlendFactor::eOneMinusSrc1Color;
        case BlendFactor::Src1Alpha:           return vk::BlendFactor::eSrc1Alpha;
        case BlendFactor::OneMinusSrc1Alpha:   return vk::BlendFactor::eOneMinusSrc1Alpha;
        }
        return vk::BlendFactor::eOne;
    }

    [[nodiscard]] constexpr vk::BlendOp toVkBlendOp(BlendOp op)
    {
        switch (op)
        {
        case BlendOp::Add:             return vk::BlendOp::eAdd;
        case BlendOp::Subtract:        return vk::BlendOp::eSubtract;
        case BlendOp::ReverseSubtract: return vk::BlendOp::eReverseSubtract;
        case BlendOp::Min:             return vk::BlendOp::eMin;
        case BlendOp::Max:             return vk::BlendOp::eMax;
        }
        return vk::BlendOp::eAdd;
    }

    [[nodiscard]] constexpr vk::LogicOp toVkLogicOp(LogicOp op)
    {
        switch (op)
        {
        case LogicOp::Clear:        return vk::LogicOp::eClear;
        case LogicOp::Set:          return vk::LogicOp::eSet;
        case LogicOp::Copy:         return vk::LogicOp::eCopy;
        case LogicOp::CopyInverted: return vk::LogicOp::eCopyInverted;
        case LogicOp::NoOp:         return vk::LogicOp::eNoOp;
        case LogicOp::Invert:       return vk::LogicOp::eInvert;
        case LogicOp::And:          return vk::LogicOp::eAnd;
        case LogicOp::NAnd:         return vk::LogicOp::eNand;
        case LogicOp::Or:           return vk::LogicOp::eOr;
        case LogicOp::NOr:          return vk::LogicOp::eNor;
        case LogicOp::Xor:          return vk::LogicOp::eXor;
        case LogicOp::Equivalent:   return vk::LogicOp::eEquivalent;
        case LogicOp::AndReverse:   return vk::LogicOp::eAndReverse;
        case LogicOp::AndInverted:  return vk::LogicOp::eAndInverted;
        case LogicOp::OrReverse:    return vk::LogicOp::eOrReverse;
        case LogicOp::OrInverted:   return vk::LogicOp::eOrInverted;
        }
        return vk::LogicOp::eCopy;
    }

    [[nodiscard]] constexpr vk::ColorComponentFlags toVkColorWriteMask(ColorWriteMask mask)
    {
        vk::ColorComponentFlags out{};
        if (hasFlag(mask, ColorWriteMask::R)) out |= vk::ColorComponentFlagBits::eR;
        if (hasFlag(mask, ColorWriteMask::G)) out |= vk::ColorComponentFlagBits::eG;
        if (hasFlag(mask, ColorWriteMask::B)) out |= vk::ColorComponentFlagBits::eB;
        if (hasFlag(mask, ColorWriteMask::A)) out |= vk::ColorComponentFlagBits::eA;
        return out;
    }

    [[nodiscard]] constexpr vk::SampleCountFlagBits toVkSampleCount(SampleCount s)
    {
        switch (s)
        {
        case SampleCount::Count1:  return vk::SampleCountFlagBits::e1;
        case SampleCount::Count2:  return vk::SampleCountFlagBits::e2;
        case SampleCount::Count4:  return vk::SampleCountFlagBits::e4;
        case SampleCount::Count8:  return vk::SampleCountFlagBits::e8;
        case SampleCount::Count16: return vk::SampleCountFlagBits::e16;
        }
        return vk::SampleCountFlagBits::e1;
    }

    // ImageDesc helpers

    [[nodiscard]] constexpr vk::ImageUsageFlags toVkImageUsage(ImageUsage usage)
    {
        vk::ImageUsageFlags out{};
        if (hasUsage(usage, ImageUsage::Sampled))             out |= vk::ImageUsageFlagBits::eSampled;
        if (hasUsage(usage, ImageUsage::Storage))             out |= vk::ImageUsageFlagBits::eStorage;
        if (hasUsage(usage, ImageUsage::ColorAttachment))     out |= vk::ImageUsageFlagBits::eColorAttachment;
        if (hasUsage(usage, ImageUsage::DepthAttachment))     out |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
        if (hasUsage(usage, ImageUsage::StencilAttachment))   out |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
        if (hasUsage(usage, ImageUsage::InputAttachment))     out |= vk::ImageUsageFlagBits::eInputAttachment;
        if (hasUsage(usage, ImageUsage::CopySrc))             out |= vk::ImageUsageFlagBits::eTransferSrc;
        if (hasUsage(usage, ImageUsage::CopyDst))             out |= vk::ImageUsageFlagBits::eTransferDst;
        if (hasUsage(usage, ImageUsage::TransientAttachment)) out |= vk::ImageUsageFlagBits::eTransientAttachment;
        if (hasUsage(usage, ImageUsage::ShadingRate))         out |= vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR;
        return out;
    }

    [[nodiscard]] constexpr vk::ImageType toVkImageType(ImageType t)
    {
        switch (t)
        {
        case ImageType::Image1D: return vk::ImageType::e1D;
        case ImageType::Image2D: return vk::ImageType::e2D;
        case ImageType::Image3D: return vk::ImageType::e3D;
        }
        return vk::ImageType::e2D;
    }

    [[nodiscard]] constexpr vk::ImageTiling toVkImageTiling(ImageTiling t)
    {
        return t == ImageTiling::Linear ? vk::ImageTiling::eLinear : vk::ImageTiling::eOptimal;
    }

    [[nodiscard]] constexpr vk::ImageLayout toVkImageLayout(ImageLayout l)
    {
        switch (l)
        {
        case ImageLayout::Undefined:                return vk::ImageLayout::eUndefined;
        case ImageLayout::General:                  return vk::ImageLayout::eGeneral;
        case ImageLayout::ColorAttachment:          return vk::ImageLayout::eColorAttachmentOptimal;
        case ImageLayout::DepthStencilAttachment:   return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ImageLayout::DepthStencilReadOnly:     return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        case ImageLayout::ShaderReadOnly:           return vk::ImageLayout::eShaderReadOnlyOptimal;
        case ImageLayout::StorageReadWrite:         return vk::ImageLayout::eGeneral;
        case ImageLayout::StorageReadOnly:          return vk::ImageLayout::eGeneral;
        case ImageLayout::CopySrc:                  return vk::ImageLayout::eTransferSrcOptimal;
        case ImageLayout::CopyDst:                  return vk::ImageLayout::eTransferDstOptimal;
        case ImageLayout::Present:                  return vk::ImageLayout::ePresentSrcKHR;
        case ImageLayout::ShadingRate:              return vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR;
        }
        return vk::ImageLayout::eUndefined;
    }

    // Helper ColorWriteMask bit check
    [[nodiscard]] constexpr bool hasFlag(ColorWriteMask mask, ColorWriteMask bit)
    {
        return (mask & bit) != ColorWriteMask::None;
    }

}
