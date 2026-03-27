#pragma once
#include <cstdint>
#include <cstring>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Scene/SceneTypes.h>

namespace Zero
{
    enum class SceneCommandType : uint8_t
    {
        AddRenderable,
        RemoveRenderable,
        UpdateTransform,
        UpdateBounds,
        AddDirectionalLight,
        AddPointLight,
        RemoveLight,
        UpdatePointLightPosition,
        SetCamera,
    };

    // Trivial version of RenderableDesc — stores handle IDs as uint32_t
    // SceneData::Insert reconstructs RHI handles from these IDs.
    struct RenderableDescPOD
    {
        Vec3     boundsMin;
        Vec3     boundsMax;
        Mat4     transform;

        uint32_t vertexBufferId = 0;   // BufferHandle::GetId()
        uint32_t indexBufferId = 0;   // BufferHandle::GetId()
        uint32_t pipelineId_ = 0;   // PipelineHandle::GetId()

        uint32_t indexCount = 0;
        uint32_t firstIndex = 0;
        uint32_t vertexOffset = 0;

        uint16_t materialId = 0;
        uint16_t pipelineId = 0;

        bool     castsShadow = true;
        bool     isTransparent = false;
    };

    // SceneCommand
    struct SceneCommand
    {
        SceneCommandType type = SceneCommandType::AddRenderable;
        uint32_t         handleId = 0;  // pre-assigned by Scene API, used as result

        union
        {
            RenderableDescPOD        renderable;    // AddRenderable

            struct { Mat4 transform; }              updateTransform;
            struct { Vec3 min; Vec3 max; }          updateBounds;

            DirectionalLightDesc                    dirLight;
            PointLightDesc                          pointLight;

            struct { Vec3 position; }               updatePointLight;

            CameraDesc                              camera;
        };

        SceneCommand() { std::memset(this, 0, sizeof(*this)); }
    };
}
