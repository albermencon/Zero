#pragma once
#include <vector>
#include <cstdint>
#include <Engine/Core.h>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Scene/SceneTypes.h>
#include <Engine/Scene/Scene.h>
#include "RHITypes.h"
#include "UploadRequest.h"

namespace Zero
{
    struct SceneRange
    {
        uint32_t offset = 0;
        uint32_t count = 0;
        uint32_t cameraIndex = 0;
        RenderPath path = RenderPath::CPUDriven;
    };

    struct DirectionalLightData
    {
        Vec3  direction;
        float intensity = 1.f;
        Vec3  color;
        float _pad = 0.f;
    };

    struct PointLightData
    {
        Vec3  position;
        float radius = 10.f;
        Vec3  color;
        float intensity = 1.f;
    };

    struct GPUDrivenBatch
    {
        RHI::BufferHandle   objectDataBuffer;  // SSBO
        RHI::BufferHandle   indirectBuffer;    // filled by cull compute
        RHI::BufferHandle   countBuffer;       // for vkCmdDrawIndexedIndirectCount
        RHI::PipelineHandle cullPipeline;      // compute cull PSO
        uint32_t            maxObjects = 0;

        void Reserve(uint32_t n) { maxObjects = n; }
    };

    struct RenderableSOA
    {
        // HOT
        std::vector<float> boundsMinX, boundsMinY, boundsMinZ;
        std::vector<float> boundsMaxX, boundsMaxY, boundsMaxZ;

        // WARM
        std::vector<uint32_t> visibilityMask; // packed bits
        std::vector<uint64_t> sortKeys; // built post-cull by RenderThread
        std::vector<uint32_t> transformIndex; // Index into transforms[] — multiple entries can share the same transform

        // COLD
        std::vector<RHI::BufferHandle> vertexBuffers; // same handle for same mesh
        std::vector<RHI::BufferHandle> indexBuffers;  // same handle for same mesh
        std::vector<uint32_t> indexCount;
        std::vector<uint32_t> firstIndex;
        std::vector<uint32_t> vertexOffset;

        // Material — RenderThread resolves materialId -> pipeline + descriptors
        std::vector<uint16_t> materialId;

        std::vector<uint8_t> castsShadow;
        std::vector<uint8_t> isTransparent;

        // Transforms — indexed via transformIndex, NOT parallel to entries
        // Multiple entries share the same Mat4 when they come from the same mesh
        std::vector<Mat4> transforms;

        // Count of draw entries (parallel arrays)
        [[nodiscard]] uint32_t Count() const noexcept
        {
            return static_cast<uint32_t>(boundsMinX.size());
        }

        // Count of unique transforms
        [[nodiscard]] uint32_t TransformCount() const noexcept
        {
            return static_cast<uint32_t>(transforms.size());
        }

        void Reserve(uint32_t n, uint32_t transformCount)
        {
            boundsMinX.reserve(n); boundsMinY.reserve(n); boundsMinZ.reserve(n);
            boundsMaxX.reserve(n); boundsMaxY.reserve(n); boundsMaxZ.reserve(n);
            visibilityMask.reserve((n + 31) / 32);
            sortKeys.reserve(n);
            transformIndex.reserve(n);
            vertexBuffers.reserve(n); indexBuffers.reserve(n);
            indexCount.reserve(n); firstIndex.reserve(n); vertexOffset.reserve(n);
            materialId.reserve(n);
            castsShadow.reserve(n); isTransparent.reserve(n);
            transforms.reserve(transformCount);
        }
    };

    struct FrameData
    {
        uint64_t frameIndex = 0;
        float    deltaTime = 0.f;

        // One CameraDesc per active scene — RenderThread derives viewProj locally
        uint32_t   sceneCount = 0;
        SceneRange sceneRanges[MaxActiveScenes];
        CameraDesc cameras[MaxActiveScenes];   // CameraDesc from Scene.h

        RenderableSOA  cpuDriven;   // CPU culled
        GPUDrivenBatch gpuDriven;   // compute culled, indirect draw

        std::vector<DirectionalLightData> dirLights;
        std::vector<PointLightData> pointLights;
        std::vector<UploadRequest> pendingUploads;

        void Reserve(uint32_t cpuDrawCount, uint32_t cpuTransformCount,
            uint32_t gpuBatchCount = 0, uint32_t uploadCount = 0)
        {
            cpuDriven.Reserve(cpuDrawCount, cpuTransformCount);
            gpuDriven.Reserve(gpuBatchCount);
            pendingUploads.reserve(uploadCount);
        }

        // no copy
        // allow moving
        FrameData() = default;
        FrameData(const FrameData&) = delete;
        FrameData& operator=(const FrameData&) = delete;
        FrameData(FrameData&&) = default;
        FrameData& operator=(FrameData&&) = default;
    };
}
