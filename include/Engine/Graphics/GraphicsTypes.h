#pragma once
#include <Engine/Core.h>
#include <cstdint>

namespace Zero
{
    // Primitive Topology
    enum class ENGINE_API PrimitiveTopology : uint8_t
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        PatchList,       // tessellation
    };

    // Pixel / Texture Formats
    enum class ENGINE_API PixelFormat : uint8_t
    {
        Undefined,

        // 8-bit
        R8_UNorm,
        R8_SNorm,
        R8_UInt,
        R8_SInt,

        // 16-bit
        RG8_UNorm,
        RG8_SNorm,
        R16_Float,
        R16_UInt,
        R16_SInt,

        // 32-bit
        RGBA8_UNorm,
        RGBA8_UNorm_sRGB,
        RGBA8_SNorm,
        RGBA8_UInt,
        RGBA8_SInt,
        BGRA8_UNorm,
        BGRA8_UNorm_sRGB,
        RGB10A2_UNorm,
        RG11B10_Float,
        R32_Float,
        R32_UInt,
        R32_SInt,

        // 64-bit
        RG32_Float,
        RG32_UInt,
        RG32_SInt,
        RGBA16_Float,
        RGBA16_UNorm,

        // 128-bit
        RGBA32_Float,
        RGBA32_UInt,
        RGBA32_SInt,

        // Depth/Stencil
        D16_UNorm,
        D24_UNorm_S8_UInt,
        D32_Float,
        D32_Float_S8_UInt,
    };

    // Sample Count (MSAA)
    enum class ENGINE_API SampleCount : uint8_t
    {
        Count1 = 1,
        Count2 = 2,
        Count4 = 4,
        Count8 = 8,
        Count16 = 16,
    };

    // Vertex Attribute Formats
    enum class ENGINE_API VertexFormat : uint8_t
    {
        // Floats
        Float1,
        Float2,
        Float3,
        Float4,

        // Half-floats
        Half2,
        Half4,

        // Unsigned normalized [0, 1]
        UByte4_Norm,
        UShort2_Norm,
        UShort4_Norm,

        // Signed normalized [-1, 1]
        Byte4_Norm,
        Short2_Norm,
        Short4_Norm,

        // Raw integers
        UByte4,
        UInt1,
        UInt2,
        UInt3,
        UInt4,
        Int1,
        Int2,
        Int3,
        Int4,
    };

    enum class ENGINE_API VertexInputRate : uint8_t
    {
        PerVertex,
        PerInstance,
    };

    // Rasterizer State Enums
    enum class ENGINE_API FillMode : uint8_t
    {
        Solid,
        Wireframe,
        Point,
    };

    enum class ENGINE_API CullMode : uint8_t
    {
        None,
        Front,
        Back,
        FrontAndBack,   // Vulkan only - useful for depth-pass geometry
    };

    enum class ENGINE_API FrontFace : uint8_t
    {
        CounterClockwise,
        Clockwise,
    };

    // Depth / Stencil State Enums
    enum class ENGINE_API CompareOp : uint8_t
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always,
    };

    enum class ENGINE_API StencilOp : uint8_t
    {
        Keep,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap,
    };

    // Blend State Enums
    enum class ENGINE_API BlendFactor : uint8_t
    {
        Zero,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
        ConstantColor,
        OneMinusConstantColor,
        SrcAlphaSaturate,
        Src1Color,
        OneMinusSrc1Color,
        Src1Alpha,
        OneMinusSrc1Alpha,
    };

    enum class ENGINE_API BlendOp : uint8_t
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    enum class ENGINE_API LogicOp : uint8_t
    {
        Clear,
        Set,
        Copy,
        CopyInverted,
        NoOp,
        Invert,
        And,
        NAnd,
        Or,
        NOr,
        Xor,
        Equivalent,
        AndReverse,
        AndInverted,
        OrReverse,
        OrInverted,
    };

    // Bitmask
    enum class ENGINE_API ColorWriteMask : uint8_t
    {
        None = 0,
        R = 1 << 0,
        G = 1 << 1,
        B = 1 << 2,
        A = 1 << 3,
        All = R | G | B | A,
        RGB = R | G | B,
    };

    inline constexpr ColorWriteMask operator|(ColorWriteMask a, ColorWriteMask b)
    {
        return static_cast<ColorWriteMask>(
            static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }
    inline constexpr ColorWriteMask operator&(ColorWriteMask a, ColorWriteMask b)
    {
        return static_cast<ColorWriteMask>(
            static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }
}
