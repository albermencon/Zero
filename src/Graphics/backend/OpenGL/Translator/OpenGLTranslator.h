#pragma once
#include <glad/glad.h>
#include <Engine/Graphics/GraphicsTypes.h>
#include <Engine/Graphics/ImageDesc.h>
#include <Engine/Graphics/VertexLayout.h>
#include <Engine/Graphics/BufferUsage.h>
#include <Engine/Graphics/MemoryDomain.h>
#include <Engine/Graphics/PipelineDesc.h>

namespace Zero::OpenGL
{
    [[nodiscard]] constexpr GLenum toGLInternalFormat(PixelFormat fmt)
    {
        switch (fmt)
        {
        case PixelFormat::Undefined:            return GL_NONE;
        case PixelFormat::R8_UNorm:             return GL_R8;
        case PixelFormat::R8_SNorm:             return GL_R8_SNORM;
        case PixelFormat::R8_UInt:              return GL_R8UI;
        case PixelFormat::R8_SInt:              return GL_R8I;
        case PixelFormat::RG8_UNorm:            return GL_RG8;
        case PixelFormat::RG8_SNorm:            return GL_RG8_SNORM;
        case PixelFormat::R16_Float:            return GL_R16F;
        case PixelFormat::R16_UInt:             return GL_R16UI;
        case PixelFormat::R16_SInt:             return GL_R16I;
        case PixelFormat::RGBA8_UNorm:          return GL_RGBA8;
        case PixelFormat::RGBA8_UNorm_sRGB:     return GL_SRGB8_ALPHA8;
        case PixelFormat::RGBA8_SNorm:          return GL_RGBA8_SNORM;
        case PixelFormat::RGBA8_UInt:           return GL_RGBA8UI;
        case PixelFormat::RGBA8_SInt:           return GL_RGBA8I;
        case PixelFormat::BGRA8_UNorm:          return GL_RGBA8;
        case PixelFormat::BGRA8_UNorm_sRGB:     return GL_SRGB8_ALPHA8;
        case PixelFormat::RGB10A2_UNorm:        return GL_RGB10_A2;
        case PixelFormat::RG11B10_Float:        return GL_R11F_G11F_B10F;
        case PixelFormat::R32_Float:            return GL_R32F;
        case PixelFormat::R32_UInt:             return GL_R32UI;
        case PixelFormat::R32_SInt:             return GL_R32I;
        case PixelFormat::RG32_Float:           return GL_RG32F;
        case PixelFormat::RG32_UInt:            return GL_RG32UI;
        case PixelFormat::RG32_SInt:            return GL_RG32I;
        case PixelFormat::RGBA16_Float:         return GL_RGBA16F;
        case PixelFormat::RGBA16_UNorm:         return GL_RGBA16;
        case PixelFormat::RGBA32_Float:         return GL_RGBA32F;
        case PixelFormat::RGBA32_UInt:          return GL_RGBA32UI;
        case PixelFormat::RGBA32_SInt:          return GL_RGBA32I;
        case PixelFormat::D16_UNorm:            return GL_DEPTH_COMPONENT16;
        case PixelFormat::D24_UNorm_S8_UInt:    return GL_DEPTH24_STENCIL8;
        case PixelFormat::D32_Float:            return GL_DEPTH_COMPONENT32F;
        case PixelFormat::D32_Float_S8_UInt:    return GL_DEPTH32F_STENCIL8;
        }
        return GL_NONE;
    }

    struct GLVertexFormatInfo
    {
        GLint size;
        GLenum type;
        GLboolean normalized;
    };

    [[nodiscard]] constexpr GLVertexFormatInfo toGLVertexFormat(VertexFormat fmt)
    {
        switch (fmt)
        {
        case VertexFormat::Float1:       return { 1, GL_FLOAT, GL_FALSE };
        case VertexFormat::Float2:       return { 2, GL_FLOAT, GL_FALSE };
        case VertexFormat::Float3:       return { 3, GL_FLOAT, GL_FALSE };
        case VertexFormat::Float4:       return { 4, GL_FLOAT, GL_FALSE };
        case VertexFormat::Half2:        return { 2, GL_HALF_FLOAT, GL_FALSE };
        case VertexFormat::Half4:        return { 4, GL_HALF_FLOAT, GL_FALSE };
        case VertexFormat::UByte4_Norm:  return { 4, GL_UNSIGNED_BYTE, GL_TRUE };
        case VertexFormat::UShort2_Norm: return { 2, GL_UNSIGNED_SHORT, GL_TRUE };
        case VertexFormat::UShort4_Norm: return { 4, GL_UNSIGNED_SHORT, GL_TRUE };
        case VertexFormat::Byte4_Norm:   return { 4, GL_BYTE, GL_TRUE };
        case VertexFormat::Short2_Norm:  return { 2, GL_SHORT, GL_TRUE };
        case VertexFormat::Short4_Norm:  return { 4, GL_SHORT, GL_TRUE };
        case VertexFormat::UByte4:       return { 4, GL_UNSIGNED_BYTE, GL_FALSE };
        case VertexFormat::UInt1:        return { 1, GL_UNSIGNED_INT, GL_FALSE };
        case VertexFormat::UInt2:        return { 2, GL_UNSIGNED_INT, GL_FALSE };
        case VertexFormat::UInt3:        return { 3, GL_UNSIGNED_INT, GL_FALSE };
        case VertexFormat::UInt4:        return { 4, GL_UNSIGNED_INT, GL_FALSE };
        case VertexFormat::Int1:         return { 1, GL_INT, GL_FALSE };
        case VertexFormat::Int2:         return { 2, GL_INT, GL_FALSE };
        case VertexFormat::Int3:         return { 3, GL_INT, GL_FALSE };
        case VertexFormat::Int4:         return { 4, GL_INT, GL_FALSE };
        }
        return { 0, GL_NONE, GL_FALSE };
    }

    [[nodiscard]] constexpr GLuint toGLInputRateDivisor(VertexInputRate rate)
    {
        return rate == VertexInputRate::PerInstance ? 1 : 0;
    }

    [[nodiscard]] constexpr GLenum toGLTopology(PrimitiveTopology t)
    {
        switch (t)
        {
        case PrimitiveTopology::PointList:     return GL_POINTS;
        case PrimitiveTopology::LineList:      return GL_LINES;
        case PrimitiveTopology::LineStrip:     return GL_LINE_STRIP;
        case PrimitiveTopology::TriangleList:  return GL_TRIANGLES;
        case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        case PrimitiveTopology::PatchList:     return GL_PATCHES;
        }
        return GL_TRIANGLES;
    }

    [[nodiscard]] constexpr GLenum toGLPolygonMode(FillMode m)
    {
        switch (m)
        {
        case FillMode::Solid:      return GL_FILL;
        case FillMode::Wireframe:  return GL_LINE;
        case FillMode::Point:      return GL_POINT;
        }
        return GL_FILL;
    }

    [[nodiscard]] constexpr GLenum toGLCullMode(CullMode m)
    {
        switch (m)
        {
        case CullMode::None:         return GL_NONE;
        case CullMode::Front:        return GL_FRONT;
        case CullMode::Back:         return GL_BACK;
        case CullMode::FrontAndBack: return GL_FRONT_AND_BACK;
        }
        return GL_BACK;
    }

    [[nodiscard]] constexpr GLenum toGLFrontFace(FrontFace f)
    {
        return f == FrontFace::Clockwise ? GL_CW : GL_CCW;
    }

    [[nodiscard]] constexpr GLenum toGLCompareOp(CompareOp op)
    {
        switch (op)
        {
        case CompareOp::Never:        return GL_NEVER;
        case CompareOp::Less:         return GL_LESS;
        case CompareOp::Equal:        return GL_EQUAL;
        case CompareOp::LessEqual:    return GL_LEQUAL;
        case CompareOp::Greater:      return GL_GREATER;
        case CompareOp::NotEqual:     return GL_NOTEQUAL;
        case CompareOp::GreaterEqual: return GL_GEQUAL;
        case CompareOp::Always:       return GL_ALWAYS;
        }
        return GL_LESS;
    }

    [[nodiscard]] constexpr GLenum toGLStencilOp(StencilOp op)
    {
        switch (op)
        {
        case StencilOp::Keep:           return GL_KEEP;
        case StencilOp::Zero:           return GL_ZERO;
        case StencilOp::Replace:        return GL_REPLACE;
        case StencilOp::IncrementClamp: return GL_INCR;
        case StencilOp::DecrementClamp: return GL_DECR;
        case StencilOp::Invert:         return GL_INVERT;
        case StencilOp::IncrementWrap:  return GL_INCR_WRAP;
        case StencilOp::DecrementWrap:  return GL_DECR_WRAP;
        }
        return GL_KEEP;
    }

    [[nodiscard]] constexpr GLenum toGLBlendFactor(BlendFactor f)
    {
        switch (f)
        {
        case BlendFactor::Zero:                  return GL_ZERO;
        case BlendFactor::One:                   return GL_ONE;
        case BlendFactor::SrcColor:              return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:      return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:              return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor:      return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:              return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:      return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:              return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:      return GL_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor:         return GL_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::SrcAlphaSaturate:      return GL_SRC_ALPHA_SATURATE;
        case BlendFactor::Src1Color:             return GL_SRC1_COLOR;
        case BlendFactor::OneMinusSrc1Color:     return GL_ONE_MINUS_SRC1_COLOR;
        case BlendFactor::Src1Alpha:             return GL_SRC1_ALPHA;
        case BlendFactor::OneMinusSrc1Alpha:     return GL_ONE_MINUS_SRC1_ALPHA;
        }
        return GL_ONE;
    }

    [[nodiscard]] constexpr GLenum toGLBlendOp(BlendOp op)
    {
        switch (op)
        {
        case BlendOp::Add:             return GL_FUNC_ADD;
        case BlendOp::Subtract:        return GL_FUNC_SUBTRACT;
        case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::Min:             return GL_MIN;
        case BlendOp::Max:             return GL_MAX;
        }
        return GL_FUNC_ADD;
    }

    [[nodiscard]] constexpr GLenum toGLLogicOp(LogicOp op)
    {
        switch (op)
        {
        case LogicOp::Clear:        return GL_CLEAR;
        case LogicOp::Set:          return GL_SET;
        case LogicOp::Copy:         return GL_COPY;
        case LogicOp::CopyInverted: return GL_COPY_INVERTED;
        case LogicOp::NoOp:         return GL_NOOP;
        case LogicOp::Invert:       return GL_INVERT;
        case LogicOp::And:          return GL_AND;
        case LogicOp::NAnd:         return GL_NAND;
        case LogicOp::Or:           return GL_OR;
        case LogicOp::NOr:          return GL_NOR;
        case LogicOp::Xor:          return GL_XOR;
        case LogicOp::Equivalent:   return GL_EQUIV;
        case LogicOp::AndReverse:   return GL_AND_REVERSE;
        case LogicOp::AndInverted:  return GL_AND_INVERTED;
        case LogicOp::OrReverse:    return GL_OR_REVERSE;
        case LogicOp::OrInverted:   return GL_OR_INVERTED;
        }
        return GL_COPY;
    }

    [[nodiscard]] constexpr GLbitfield toGLBufferStorageFlags(MemoryDomain domain)
    {
        GLbitfield flags = GL_DYNAMIC_STORAGE_BIT;
        switch (domain)
        {
        case MemoryDomain::GPU:
            break;
        case MemoryDomain::CPUtoGPU:
            flags |= GL_MAP_WRITE_BIT;
            break;
        case MemoryDomain::GPUtoCPU:
            flags |= GL_MAP_READ_BIT;
            break;
        case MemoryDomain::CPUtoGPU_Coherent:
            flags |= GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            break;
        }
        return flags;
    }
}
