#include "pch.h"
#include <algorithm>
#include <Engine/Ecs/Systems/RenderSyncSystem.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/EcsCommandQueue.h>
#include <Engine/Ecs/Scheduler.h>
#include <Engine/Ecs/Components/TransformComponent.h>
#include <Engine/Ecs/Components/PreviousTransformComponent.h>
#include <Engine/Ecs/Components/MeshComponent.h>
#include <Engine/Ecs/Components/BoundsComponent.h>
#include <Engine/Ecs/Components/RenderableHandleComponent.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/SceneTypes.h>

namespace Zero
{
    namespace
    {
        [[nodiscard]] Mat4 BuildRelativeMat4(const TransformComponent& tf, const Vec3& relPos) noexcept
        {
            Mat4 m = tf.rotation.ToMat4();

            m.m[0]  *= tf.scale.x;
            m.m[1]  *= tf.scale.x;
            m.m[2]  *= tf.scale.x;

            m.m[4]  *= tf.scale.y;
            m.m[5]  *= tf.scale.y;
            m.m[6]  *= tf.scale.y;

            m.m[8]  *= tf.scale.z;
            m.m[9]  *= tf.scale.z;
            m.m[10] *= tf.scale.z;

            m.m[12] = relPos.x;
            m.m[13] = relPos.y;
            m.m[14] = relPos.z;
            m.m[15] = 1.0f;

            return m;
        }
    }

    RenderSyncSystem::RenderSyncSystem(Scene& scene, MeshResolverFn resolver)
        : m_scene(scene)
        , m_resolver(std::move(resolver))
    {
    }

    void RenderSyncSystem::Execute(EcsRegistry& reg,
                                   EcsCommandQueue& queue,
                                   float /*dt*/,
                                   uint64_t epoch)
    {
        auto& handlePool = reg.Pool<RenderableHandleComponent>();
        const auto& prevPool   = reg.Pool<PreviousTransformComponent>();
        const auto& boundsPool = reg.Pool<BoundsComponent>();
        const float alpha      = reg.InterpolationAlpha();
        const RVec3 cameraWorldPos = m_scene.GetCameraPosition();
        const bool  cameraMoved    = (cameraWorldPos != m_lastCameraPos);

        // Pass A — Removals (tombstones + mesh-stripped live entities)
        //
        // 1. Drain RenderableHandleComponent tombstones populated during
        //    FlushCommands() when entities are destroyed or handles are
        //    explicitly removed. This prevents destroyed entities from
        //    leaking Scene renderables.
        // 2. For live entities whose MeshComponent was removed while they
        //    still carry a RenderableHandleComponent, remove from Scene,
        //    zero the handle in-place (preventing double-remove on the
        //    subsequent tombstone), and enqueue handle component removal.
        {
            for (const auto& removed : handlePool.Removed())
            {
                if (removed.value.id == 0) continue;

                RenderableHandle h;
                h.id = removed.value.id;
                m_scene.RemoveRenderable(h);
            }

            auto handleWrite = handlePool.WriteView(epoch, WriteSource::System);
            const uint32_t hCount = handleWrite.Count();
            for (uint32_t i = 0; i < hCount; ++i)
            {
                const uint32_t entityIdx = handleWrite.entities[i];
                if (reg.Mask(entityIdx).Has(ComponentId<MeshComponent>())) continue;

                if (handleWrite.data[i].id != 0)
                {
                    RenderableHandle h;
                    h.id = handleWrite.data[i].id;
                    m_scene.RemoveRenderable(h);

                    // Clear id before enqueueing removal so the resulting tombstone is a no-op.
                    handleWrite.data[i].id = 0;
                    handleWrite.MarkDirty(i);
                }

                Entity e = reg.CurrentGeneration(entityIdx);
                reg.RemoveComponent<RenderableHandleComponent>(e, queue);
            }
        }

        // Pass B — Additions
        // Entities with MeshComponent + TransformComponent but no
        // RenderableHandleComponent -> create Scene renderable.
        {
            ComponentView<MeshComponent, TransformComponent> view(reg);
            view.template EachExcluding<RenderableHandleComponent>(
                [&](Entity e, const MeshComponent& mesh, const TransformComponent& tf)
            {
                MeshResolvedDesc resolved;
                if (!m_resolver(mesh.meshId, mesh.materialId, resolved))
                    return;

                const uint32_t entityIdx = Index(e);
                const PreviousTransformComponent* prev = prevPool.Get(entityIdx);
                const TransformComponent interp = prev
                    ? prev->InterpolateTo(tf, alpha)
                    : tf;
                const Vec3 relPos = ToVec3(interp.position - cameraWorldPos);
                const Mat4 worldMat = BuildRelativeMat4(interp, relPos);

                RenderableDesc desc;
                desc.boundsMin     = {};
                desc.boundsMax     = {};
                desc.transform     = worldMat;
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

                if (const BoundsComponent* bounds = boundsPool.Get(entityIdx))
                {
                    desc.boundsMin = bounds->min;
                    desc.boundsMax = bounds->max;
                }

                RenderableHandle handle = m_scene.AddRenderable(desc);
                reg.AddComponent(e, RenderableHandleComponent{ handle.id }, queue);
            });
        }

        // Pass C — Dirty updates (monotonic epoch + TRS interpolation)
        // Walk TransformComponent and BoundsComponent via bulk-span ReadView.
        // Emit Scene updates when modifiedEpoch > m_lastSyncedEpoch, or
        // when an interpolated dynamic entity has prev != curr.
        {
            const auto tfView = reg.Pool<TransformComponent>().ReadView();
            const uint32_t tfCount = tfView.Count();
            for (uint32_t i = 0; i < tfCount; ++i)
            {
                const uint32_t entityIdx = tfView.entities[i];
                const RenderableHandleComponent* rh = handlePool.Get(entityIdx);
                if (!rh || rh->id == 0) continue;

                const PreviousTransformComponent* prev = prevPool.Get(entityIdx);
                const bool epochDirty   = tfView.IsDirtySince(i, m_lastSyncedEpoch);
                const bool interpActive = prev != nullptr && prev->DiffersFrom(tfView.data[i]);

                if (!epochDirty && !interpActive && !cameraMoved) continue;

                const TransformComponent interp = prev
                    ? prev->InterpolateTo(tfView.data[i], alpha)
                    : tfView.data[i];
                const Vec3 relPos = ToVec3(interp.position - cameraWorldPos);
                const Mat4 worldMat = BuildRelativeMat4(interp, relPos);

                RenderableHandle h;
                h.id = rh->id;
                m_scene.UpdateTransform(h, worldMat);
            }
        }

        {
            const auto boundsView = boundsPool.ReadView();
            const uint32_t bCount = boundsView.Count();
            for (uint32_t i = 0; i < bCount; ++i)
            {
                if (!boundsView.IsDirtySince(i, m_lastSyncedEpoch)) continue;

                const uint32_t entityIdx = boundsView.entities[i];
                const RenderableHandleComponent* rh = handlePool.Get(entityIdx);
                if (!rh || rh->id == 0) continue;

                RenderableHandle h;
                h.id = rh->id;
                m_scene.UpdateBounds(h, boundsView.data[i].min, boundsView.data[i].max);
            }
        }

        m_lastCameraPos   = cameraWorldPos;
        m_lastSyncedEpoch = std::max(m_lastSyncedEpoch, reg.CurrentEpoch());
    }

    SystemDescriptor RenderSyncSystem::Descriptor()
    {
        SystemDescriptor desc;
        desc.name     = "RenderSyncSystem";
        desc.phase    = SchedulePhase::RenderSync;
        desc.affinity = ThreadAffinity::MainThread;
        desc.access.reads = MaskOf<
            TransformComponent,
            PreviousTransformComponent,
            BoundsComponent,
            MeshComponent,
            RenderableHandleComponent>();
        desc.access.writes         = MaskOf<RenderableHandleComponent>();
        desc.access.resourceWrites = ResourceBit::Scene;
        desc.execute = [this](EcsRegistry& reg, EcsCommandQueue& queue, float dt, uint64_t epoch) {
            Execute(reg, queue, dt, epoch);
        };
        return desc;
    }
}
