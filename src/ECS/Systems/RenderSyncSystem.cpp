#include "pch.h"
#include <Engine/Ecs/Systems/RenderSyncSystem.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/Scheduler.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/SceneTypes.h>
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/BoundsComponent.h"
#include "ECS/Components/RenderableHandleComponent.h"

namespace Zero
{
    RenderSyncSystem::RenderSyncSystem(Scene& scene, EcsCommandQueue& q, MeshResolverFn resolver)
        : m_scene(scene)
        , m_queue(q)
        , m_resolver(std::move(resolver))
    {
    }

    void RenderSyncSystem::Execute(EcsRegistry& reg, float /*dt*/, uint32_t currentFrame)
    {
        // ---------------------------------------------------------------
        // Pass A — Additions
        // Entities with MeshComponent + TransformComponent but no
        // RenderableHandleComponent → need a Scene renderable created.
        // ---------------------------------------------------------------
        {
            ComponentView<MeshComponent, TransformComponent> view(reg);
            view.template EachExcluding<RenderableHandleComponent>(
                [&](Entity e, const MeshComponent& mesh, const TransformComponent& tf)
            {
                MeshResolvedDesc resolved;
                if (!m_resolver(mesh.meshId, mesh.materialId, resolved))
                    return;

                RenderableDesc desc;
                desc.boundsMin     = {};
                desc.boundsMax     = {};
                desc.transform     = tf.localToWorld;
                desc.vertexBuffer  = resolved.vertexBuffer;
                desc.indexBuffer   = resolved.indexBuffer;
                desc.pipeline      = resolved.pipeline;
                desc.indexCount    = resolved.indexCount;
                desc.firstIndex    = resolved.firstIndex;
                desc.vertexOffset  = resolved.vertexOffset;
                desc.materialId    = resolved.materialId;
                desc.pipelineId    = resolved.pipelineId;
                desc.castsShadow   = resolved.castsShadow;
                desc.isTransparent = resolved.isTransparent;

                const uint32_t entityIdx = Index(e);

                const BoundsComponent* bounds = reg.Pool<BoundsComponent>().Get(entityIdx);
                if (bounds)
                {
                    desc.boundsMin = bounds->min;
                    desc.boundsMax = bounds->max;
                }

                RenderableHandle handle = m_scene.AddRenderable(desc);
                reg.AddComponent(e, RenderableHandleComponent{ handle.id }, m_queue);
            });
        }

        // ---------------------------------------------------------------
        // Pass B — Dirty updates
        // Walk TransformComponent and BoundsComponent dense arrays.
        // Only emit Scene updates for slots written this frame.
        // ---------------------------------------------------------------
        {
            auto& handlePool = reg.Pool<RenderableHandleComponent>();
            auto& tfPool     = reg.Pool<TransformComponent>();

            const uint32_t tfCount = tfPool.Count();
            for (uint32_t i = 0; i < tfCount; ++i)
            {
                if (tfPool.modifiedFrame[i] != currentFrame) continue;

                const uint32_t entityIdx = tfPool.dense_to_entity[i];
                const RenderableHandleComponent* rh = handlePool.Get(entityIdx);
                if (!rh) continue;

                RenderableHandle h;
                h.id = rh->id;
                m_scene.UpdateTransform(h, tfPool.data[i].localToWorld);
            }
        }

        {
            auto& handlePool = reg.Pool<RenderableHandleComponent>();
            auto& boundsPool = reg.Pool<BoundsComponent>();

            const uint32_t bCount = boundsPool.Count();
            for (uint32_t i = 0; i < bCount; ++i)
            {
                if (boundsPool.modifiedFrame[i] != currentFrame) continue;

                const uint32_t entityIdx = boundsPool.dense_to_entity[i];
                const RenderableHandleComponent* rh = handlePool.Get(entityIdx);
                if (!rh) continue;

                RenderableHandle h;
                h.id = rh->id;
                m_scene.UpdateBounds(h, boundsPool.data[i].min, boundsPool.data[i].max);
            }
        }

        // ---------------------------------------------------------------
        // Pass C — Removals
        // Entities with RenderableHandleComponent but no MeshComponent
        // → the mesh was removed, so remove from Scene.
        // ---------------------------------------------------------------
        {
            auto& handlePool = reg.Pool<RenderableHandleComponent>();
            const uint32_t hCount = handlePool.Count();

            for (uint32_t i = 0; i < hCount; ++i)
            {
                const uint32_t entityIdx = handlePool.dense_to_entity[i];
                if (reg.Mask(entityIdx).Has(ComponentId<MeshComponent>())) continue;

                RenderableHandle h;
                h.id = handlePool.data[i].id;
                m_scene.RemoveRenderable(h);

                Entity e = reg.CurrentGeneration(entityIdx);
                reg.RemoveComponent<RenderableHandleComponent>(e, m_queue);
            }
        }
    }

    SystemDescriptor RenderSyncSystem::Descriptor()
    {
        SystemDescriptor desc;
        desc.name = "RenderSyncSystem";
        desc.access.reads  = MaskOf<TransformComponent, BoundsComponent, MeshComponent, RenderableHandleComponent>();
        desc.access.writes = MaskOf<RenderableHandleComponent>();
        // Wire the descriptor to the real system — previously this was a no-op
        // stub, so the scheduler dispatched a system that did nothing.
        desc.execute = [this](EcsRegistry& reg, float dt, uint32_t currentFrame) {
            Execute(reg, dt, currentFrame);
        };
        return desc;
    }
}
