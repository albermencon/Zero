#pragma once
#include <cstdint>
#include <Engine/Core.h>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Graphics/ResourceHandle.h>

namespace Zero
{
    using SceneId = uint32_t;
    static constexpr SceneId MainSceneId = 0;
    static constexpr uint32_t MaxActiveScenes = 8;

    // Scene object handles — opaque IDs returned by Scene mutation calls.
    // Stable for the lifetime of the object. Use to update or remove.

    struct ENGINE_API RenderableHandle
    {
        uint32_t id = 0;
        [[nodiscard]] bool IsValid() const noexcept { return id != 0; }
        bool operator==(const RenderableHandle&) const = default;
    };

    struct ENGINE_API LightHandle
    {
        uint32_t id = 0;
        [[nodiscard]] bool IsValid() const noexcept { return id != 0; }
        bool operator==(const LightHandle&) const = default;
    };

    // Descriptors — used to add objects to a Scene.

    struct ENGINE_API RenderableDesc
    {
        Vec3 boundsMin;
        Vec3 boundsMax;
        Mat4 transform;

        BufferHandle   vertexBuffer;
        BufferHandle   indexBuffer;
        PipelineHandle pipeline;

        uint32_t indexCount = 0;
        uint32_t firstIndex = 0;
        uint32_t vertexOffset = 0;

        uint16_t materialId = 0;
        uint16_t pipelineId = 0;

        bool castsShadow = true;
        bool isTransparent = false;
    };

    struct ENGINE_API DirectionalLightDesc
    {
        Vec3  direction = { 0.f, -1.f, 0.f };
        Vec3  color = { 1.f,  1.f, 1.f };
        float intensity = 1.f;
    };

    struct ENGINE_API PointLightDesc
    {
        Vec3  position = { 0.f, 0.f, 0.f };
        Vec3  color = { 1.f, 1.f, 1.f };
        float radius = 10.f;
        float intensity = 1.f;
    };
}
