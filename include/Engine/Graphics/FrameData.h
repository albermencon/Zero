#pragma once
#include <vector>
#include <cstdint>
#include <Engine/Core.h>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Scene/SceneTypes.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Graphics/ResourceHandle.h>
#include <Engine/Graphics/ImGuiFrame.h>
#include <Engine/Graphics/RHITypes.h>

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
        BufferHandle   objectDataBuffer;  // SSBO
        BufferHandle   indirectBuffer;    // filled by cull compute
        BufferHandle   countBuffer;       // for vkCmdDrawIndexedIndirectCount
        PipelineHandle cullPipeline;      // compute cull PSO
        uint32_t       maxObjects = 0;

        void Reserve(uint32_t n) {}
        void Clear() {}
    };

    struct FrameData
    {
        uint64_t frameIndex = 0;
        float    deltaTime = 0.f;

        Mat4     viewProj{};
        bool     wireframe = false;

        // One CameraDesc per active scene — RenderThread derives viewProj locally
        uint32_t   sceneCount = 0;
        SceneRange sceneRanges[MaxActiveScenes];
        CameraDesc cameras[MaxActiveScenes];   // CameraDesc from Scene.h

        std::vector<RHI::DrawPacket>      drawPackets; // 32-byte commands
        std::vector<RHI::GeometryBinding> geometryTable;
        std::vector<Mat4>                 transform;   // Uploaded to SSBO
        GPUDrivenBatch                    gpuDriven;   // compute culled, indirect draw

        std::vector<DirectionalLightData> dirLights;
        std::vector<PointLightData> pointLights;
        ImGuiFrame imguiFrame;

        void Reserve(uint32_t drawCount, uint32_t transformCount,
            uint32_t gpuBatchCount = 0)
        {
            drawPackets.reserve(drawCount);
            transform.reserve(transformCount);
            gpuDriven.Reserve(gpuBatchCount);
        }

        void Clear()
        {
            drawPackets.clear();
            geometryTable.clear();
            transform.clear();
            gpuDriven.Clear();
            dirLights.clear();
            pointLights.clear();
            sceneCount = 0;
            imguiFrame.Reset();
        }

        FrameData() = default;
        FrameData(const FrameData&) = delete;
        FrameData& operator=(const FrameData&) = delete;
        FrameData(FrameData&&) = default;
        FrameData& operator=(FrameData&&) = default;
    };
}
