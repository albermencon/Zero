#pragma once
#include <Engine/Core.h>
#include <cstdint>
#include <array>
#include "GraphicsTypes.h"

namespace Zero 
{
    static constexpr uint32_t MaxVertexAttributes = 16;
    static constexpr uint32_t MaxVertexBindings = 8;

    // VertexAttribute
    //   Describes a single per-vertex or per-instance data field.
    //   - location : shader input location (layout(location = N))
    //   - binding  : which vertex buffer stream this attribute belongs to
    //   - format   : data type and component count
    //   - offset   : byte offset within the binding's stride block
    struct ENGINE_API VertexAttribute
    {
        uint32_t     location = 0;
        uint32_t     binding = 0;
        VertexFormat format = VertexFormat::Float3;
        uint32_t     offset = 0;        // bytes from start of vertex element

        constexpr VertexAttribute() = default;
        constexpr VertexAttribute(uint32_t location,
            uint32_t binding,
            VertexFormat format,
            uint32_t offset)
            : location(location), binding(binding), format(format), offset(offset)
        {
        }
    };

    // VertexBinding
    //   Describes a vertex buffer stream (one VBO slot).
    //   - binding   : index matching VertexAttribute::binding
    //   - stride    : byte distance between consecutive vertex elements
    //   - inputRate : PerVertex for geometry, PerInstance for instancing
    struct ENGINE_API VertexBinding
    {
        uint32_t        binding = 0;
        uint32_t        stride = 0;
        VertexInputRate inputRate = VertexInputRate::PerVertex;

        constexpr VertexBinding() = default;
        constexpr VertexBinding(uint32_t binding,
            uint32_t stride,
            VertexInputRate inputRate = VertexInputRate::PerVertex)
            : binding(binding), stride(stride), inputRate(inputRate)
        {
        }
    };

    struct ENGINE_API VertexLayout
    {
        std::array<VertexAttribute, MaxVertexAttributes> attributes{};
        std::array<VertexBinding, MaxVertexBindings>   bindings{};
        uint32_t attributeCount = 0;
        uint32_t bindingCount = 0;

        // Builder API

        constexpr VertexLayout& binding(uint32_t bindingIndex,
            uint32_t stride,
            VertexInputRate rate = VertexInputRate::PerVertex)
        {
            ENGINE_CORE_ASSERT(bindingCount < MaxVertexBindings, "Too many vertex bindings")
            bindings[bindingCount++] = VertexBinding{ bindingIndex, stride, rate };
            return *this;
        }

        constexpr VertexLayout& attribute(uint32_t location,
            uint32_t bindingIndex,
            VertexFormat format,
            uint32_t offset)
        {
            ENGINE_CORE_ASSERT(attributeCount < MaxVertexAttributes,"Too many vertex attributes");
            attributes[attributeCount++] = VertexAttribute{ location, bindingIndex, format, offset };
            return *this;
        }

        // Introspection

        [[nodiscard]] constexpr bool empty() const { return attributeCount == 0; }
        [[nodiscard]] constexpr uint32_t numAttributes() const { return attributeCount; }
        [[nodiscard]] constexpr uint32_t numBindings()   const { return bindingCount; }

        [[nodiscard]] constexpr const VertexAttribute* attributeData() const
        {
            return attributes.data();
        }
        [[nodiscard]] constexpr const VertexBinding* bindingData() const
        {
            return bindings.data();
        }
    };

    [[nodiscard]] constexpr uint32_t vertexFormatSize(VertexFormat fmt)
    {
        switch (fmt)
        {
        case VertexFormat::Float1:       return 4;
        case VertexFormat::Float2:       return 8;
        case VertexFormat::Float3:       return 12;
        case VertexFormat::Float4:       return 16;
        case VertexFormat::Half2:        return 4;
        case VertexFormat::Half4:        return 8;
        case VertexFormat::UByte4_Norm:  return 4;
        case VertexFormat::UShort2_Norm: return 4;
        case VertexFormat::UShort4_Norm: return 8;
        case VertexFormat::Byte4_Norm:   return 4;
        case VertexFormat::Short2_Norm:  return 4;
        case VertexFormat::Short4_Norm:  return 8;
        case VertexFormat::UByte4:       return 4;
        case VertexFormat::UInt1:        return 4;
        case VertexFormat::UInt2:        return 8;
        case VertexFormat::UInt3:        return 12;
        case VertexFormat::UInt4:        return 16;
        case VertexFormat::Int1:         return 4;
        case VertexFormat::Int2:         return 8;
        case VertexFormat::Int3:         return 12;
        case VertexFormat::Int4:         return 16;
        }
        return 0;
    }

    // =========================================================================
    // Layouts
    //   Predefined VertexLayout constants for common vertex formats.
    //   All use binding 0, interleaved, PerVertex.
    //
    //   Vertex structs they match:
    //
    //   Pos3           { Vec3 position; }                              // 12 B
    //   Pos3_Col4      { Vec3 position; UByte4 color; }               // 16 B
    //   Pos3_UV2       { Vec3 position; Vec2 uv; }                    // 20 B
    //   Pos3_Norm3     { Vec3 position; Vec3 normal; }                // 24 B
    //   Pos3_Norm3_UV2 { Vec3 position; Vec3 normal; Vec2 uv; }       // 32 B
    //   Pos3_Norm3_Tan4_UV2
    //                  { Vec3 pos; Vec3 norm; Vec4 tangent; Vec2 uv; }// 48 B
    //   Pos2_UV2_Col4  { Vec2 position; Vec2 uv; UByte4 color; }      // 20 B  (UI/ImGui)
    // =========================================================================
    namespace Layouts
    {
        // --- Pos3 (12 B) -----------------------------------------------------
        // depth pre-pass, shadow maps, occlusion queries
        inline constexpr VertexLayout Pos3 = VertexLayout{}
            .binding(0, 12)
            .attribute(0, 0, VertexFormat::Float3, 0);   // position

        // --- Pos3_Col4 (16 B) ------------------------------------------------
        // debug draw, editor primitives (vertex color, no lighting)
        inline constexpr VertexLayout Pos3_Col4 = VertexLayout{}
            .binding(0, 16)
            .attribute(0, 0, VertexFormat::Float3, 0)   // position
            .attribute(1, 0, VertexFormat::UByte4_Norm, 12);  // color (RGBA8 normalised)

        // --- Pos3_UV2 (20 B) -------------------------------------------------
        // unlit textured geometry, skybox, fullscreen effects with UVs
        inline constexpr VertexLayout Pos3_UV2 = VertexLayout{}
            .binding(0, 20)
            .attribute(0, 0, VertexFormat::Float3, 0)    // position
            .attribute(1, 0, VertexFormat::Float2, 12);  // uv

        // --- Pos3_Norm3 (24 B) -----------------------------------------------
        // simple lit geometry without texture coordinates
        inline constexpr VertexLayout Pos3_Norm3 = VertexLayout{}
            .binding(0, 24)
            .attribute(0, 0, VertexFormat::Float3, 0)    // position
            .attribute(1, 0, VertexFormat::Float3, 12);  // normal

        // --- Pos3_Norm3_UV2 (32 B) -------------------------------------------
        // standard opaque PBR forward pass
        inline constexpr VertexLayout Pos3_Norm3_UV2 = VertexLayout{}
            .binding(0, 32)
            .attribute(0, 0, VertexFormat::Float3, 0)    // position
            .attribute(1, 0, VertexFormat::Float3, 12)   // normal
            .attribute(2, 0, VertexFormat::Float2, 24);  // uv

        // --- Pos3_Norm3_Tan4_UV2 (48 B) --------------------------------------
        // normal-mapped PBR (tangent.w encodes bitangent handedness)
        inline constexpr VertexLayout Pos3_Norm3_Tan4_UV2 = VertexLayout{}
            .binding(0, 48)
            .attribute(0, 0, VertexFormat::Float3, 0)    // position
            .attribute(1, 0, VertexFormat::Float3, 12)   // normal
            .attribute(2, 0, VertexFormat::Float4, 24)   // tangent (xyz + handedness w)
            .attribute(3, 0, VertexFormat::Float2, 40);  // uv

        // --- Pos2_UV2_Col4 (20 B) --------------------------------------------
        // UI / immediate-mode (matches ImDrawVert layout)
        inline constexpr VertexLayout Pos2_UV2_Col4 = VertexLayout{}
            .binding(0, 20)
            .attribute(0, 0, VertexFormat::Float2, 0)   // position
            .attribute(1, 0, VertexFormat::Float2, 8)   // uv
            .attribute(2, 0, VertexFormat::UByte4_Norm, 16); // color
    }
}
