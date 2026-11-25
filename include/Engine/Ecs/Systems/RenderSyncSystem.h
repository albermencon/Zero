#pragma once
#include <cstdint>
#include <functional>
#include <Engine/Export.h>
#include <Engine/Math/Real.h>
#include <Engine/Graphics/ResourceHandle.h>
#include <Engine/Ecs/ComponentMask.h>

namespace Zero
{
    class EcsRegistry;
    class Scene;
    struct EcsCommandQueue;
    struct SystemDescriptor;

    struct MeshResolvedDesc
    {
        BufferHandle   vertexBuffer;
        BufferHandle   indexBuffer;
        PipelineHandle pipeline;
        uint32_t       indexCount    = 0;
        uint32_t       firstIndex    = 0;
        uint32_t       vertexOffset  = 0;
        uint16_t       pipelineId    = 0;
        uint16_t       materialId    = 0;
        bool           castsShadow   = true;
        bool           isTransparent = false;
    };

    using MeshResolverFn = std::function<bool(uint32_t meshId, uint32_t materialId, MeshResolvedDesc& out)>;

    class RenderSyncSystem
    {
    public:
        ZERO_API RenderSyncSystem(Scene& scene, MeshResolverFn resolver);

        ZERO_API void Execute(EcsRegistry& reg,
                              EcsCommandQueue& queue,
                              float dt,
                              uint64_t epoch);

        [[nodiscard]] ZERO_API SystemDescriptor Descriptor();

        [[nodiscard]] uint64_t LastSyncedEpoch() const noexcept { return m_lastSyncedEpoch; }

    private:
        Scene&         m_scene;
        MeshResolverFn m_resolver;
        uint64_t       m_lastSyncedEpoch = 0;
        RVec3          m_lastCameraPos{};
    };
}
