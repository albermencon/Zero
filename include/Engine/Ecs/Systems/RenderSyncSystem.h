#pragma once
#include <cstdint>
#include <functional>
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
        uint32_t       indexCount   = 0;
        uint32_t       firstIndex   = 0;
        uint32_t       vertexOffset = 0;
        uint16_t       pipelineId   = 0;
        uint16_t       materialId   = 0;
        bool           castsShadow  = true;
        bool           isTransparent = false;
    };

    using MeshResolverFn = std::function<bool(uint32_t meshId, uint32_t materialId, MeshResolvedDesc& out)>;

    class RenderSyncSystem
    {
    public:
        RenderSyncSystem(Scene& scene, EcsCommandQueue& q, MeshResolverFn resolver);

        void Execute(EcsRegistry& reg, float dt, uint32_t currentFrame);

        SystemDescriptor Descriptor();

    private:
        Scene&           m_scene;
        EcsCommandQueue& m_queue;
        MeshResolverFn   m_resolver;
    };
}
