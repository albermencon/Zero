#include "pch.h"
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Log.h>
#include "Scene/SceneData.h"
#include <Engine/Graphics/FrameData.h>
#include <Engine/Math/Frustum.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>

namespace Zero { void Scene_FlushCommands(Scene& scene); }

namespace Zero
{
    struct SceneManager::Impl
    {
        std::vector<std::unique_ptr<Scene>> scenes;
        std::vector<SceneId>                activeIds;
        uint32_t                            nextSceneId = 1;
        static constexpr int BUFFER = 3;
        FrameData                           frames[BUFFER];
        std::vector<uint32_t>               visibleIndices;
    };

    SceneManager& SceneManager::Get()
    {
        static SceneManager instance;
        return instance;
    }

    void SceneManager::Init()
    {
        if (m_impl) return; // idempotent
        m_impl = new Impl();
        m_impl->scenes.emplace_back(new Scene(MainSceneId, "Main", RenderPath::CPUDriven));
        m_impl->activeIds.push_back(MainSceneId);
        ZERO_CORE_INFO("SceneManager initialized.");
    }

    void SceneManager::Shutdown()
    {
        m_impl->scenes.clear();
        m_impl->activeIds.clear();
        delete m_impl;
        m_impl = nullptr;
    }

    SceneId SceneManager::CreateScene(std::string_view name, RenderPath path)
    {
        SceneId id = m_impl->nextSceneId++;
        m_impl->scenes.emplace_back(new Scene(id, name, path));
        return id;
    }

    Scene* SceneManager::GetScene(SceneId id)
    {
        for (auto& s : m_impl->scenes)
            if (s->GetId() == id) return s.get();
        return nullptr;
    }

    Scene* SceneManager::GetMainScene()
    {
        return m_impl->scenes[0].get();
    }

    void SceneManager::SetSceneActive(SceneId id, bool active)
    {
        auto& ids = m_impl->activeIds;
        if (active)
        {
            for (SceneId s : ids) if (s == id) return;
            if (ids.size() < MaxActiveScenes)
                ids.push_back(id);
            else
                ZERO_CORE_WARN("MaxActiveScenes reached, cannot activate scene {0}", id);
        }
        else
        {
            ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
        }
    }

    void SceneManager::DestroyScene(SceneId id)
    {
        if (id == MainSceneId) return;
        SetSceneActive(id, false);
        auto& s = m_impl->scenes;
        s.erase(std::remove_if(s.begin(), s.end(),
            [id](const auto& sc) { return sc->GetId() == id; }), s.end());
    }

    void SceneManager::FlushCommands()
    {
        for (auto& scene : m_impl->scenes)
            Scene_FlushCommands(*scene);
    }

    template<typename T>
    static void AppendArray(std::vector<T>& dst, const std::vector<T>& src, uint32_t offset)
    {
        dst.resize(offset + src.size());
        std::memcpy(dst.data() + offset, src.data(), src.size() * sizeof(T));
    }

    FrameData* SceneManager::BuildFrame(uint64_t frameNumber, float deltaTime)
    {
        if (!m_impl) return nullptr;
        uint32_t frameIndex = frameNumber % Impl::BUFFER;
        FrameData* frame = &m_impl->frames[frameIndex];

        frame->Clear();
        frame->frameIndex = frameIndex;
        frame->deltaTime = deltaTime;

        // end of the UB

        // Count per path for single Reserve()
        uint32_t cpuDrawCount = 0, cpuTransformCount = 0, gpuBatchCount = 0;
        for (SceneId id : m_impl->activeIds)
        {
            Scene* s = GetScene(id);
            if (!s) continue;
            if (s->m_impl->renderPath == RenderPath::CPUDriven)
            {
                cpuDrawCount += s->m_impl->renderables.Count();
                cpuTransformCount += static_cast<uint32_t>(s->m_impl->renderables.transforms.size());
            }
            else
            {
                ++gpuBatchCount;
            }
        }
        //frame->Reserve(cpuDrawCount, cpuTransformCount, gpuBatchCount);

        uint32_t cpuOffset = 0;
        uint32_t sceneIdx = 0;

        for (SceneId id : m_impl->activeIds)
        {
            if (sceneIdx >= MaxActiveScenes) break;
            Scene* scene = GetScene(id);
            if (!scene) continue;

            const RenderPath path = scene->m_impl->renderPath;
            auto& store = scene->m_impl->renderables;
            const uint32_t count = store.Count();

            const auto& cam = scene->m_impl->camera;
            Mat4 viewProj = cam.proj * cam.view;
            if (sceneIdx == 0)
                frame->viewProj = viewProj;

            frame->sceneRanges[sceneIdx] = { cpuOffset, count, sceneIdx, path };
            frame->cameras[sceneIdx] = cam;
            frame->sceneCount = sceneIdx + 1;

            auto getOrAddGeometry = [frame](BufferHandle vb, BufferHandle ib) -> uint8_t {
                for (size_t g = 0; g < frame->geometryTable.size(); ++g)
                {
                    if (frame->geometryTable[g].vertexBuffer == vb && frame->geometryTable[g].indexBuffer == ib)
                        return static_cast<uint8_t>(g);
                }
                uint8_t newId = static_cast<uint8_t>(frame->geometryTable.size());
                frame->geometryTable.push_back({ vb, ib });
                return newId;
            };

            if (path == RenderPath::CPUDriven && count > 0)
            {
                Frustum frustum = Frustum::FromViewProj(viewProj);

                m_impl->visibleIndices.resize(count);
                uint32_t visibleCount = frustum.CullAABBBatch(
                    store.boundsMinX.data(), store.boundsMinY.data(), store.boundsMinZ.data(),
                    store.boundsMaxX.data(), store.boundsMaxY.data(), store.boundsMaxZ.data(),
                    count,
                    m_impl->visibleIndices.data()
                );

                // Append this scene's transforms to the global frame transform array (for the SSBO)
                const uint32_t baseTransformIndex = static_cast<uint32_t>(frame->transform.size());
                frame->transform.insert(frame->transform.end(), store.transforms.begin(), store.transforms.end());

                // Pre-allocate draw packets to avoid vector reallocations
                frame->drawPackets.reserve(frame->drawPackets.size() + visibleCount);

                const float depthRangeInv = (cam.farPlane > cam.nearPlane)
                    ? (1.0f / (cam.farPlane - cam.nearPlane))
                    : 1.0f;

                // Emit DrawPackets for visible items
                for (uint32_t v = 0; v < visibleCount; ++v)
                {
                    uint32_t i = m_impl->visibleIndices[v];

                    RHI::RenderPass pass = store.isTransparent[i] ?
                                        RHI::RenderPass::Transparent : RHI::RenderPass::Opaque;

                    const float cx = 0.5f * (store.boundsMinX[i] + store.boundsMaxX[i]);
                    const float cy = 0.5f * (store.boundsMinY[i] + store.boundsMaxY[i]);
                    const float cz = 0.5f * (store.boundsMinZ[i] + store.boundsMaxZ[i]);

                    const float viewZ = -(cam.view.m[2] * cx + cam.view.m[6] * cy + cam.view.m[10] * cz + cam.view.m[14]);
                    float depth = (viewZ - cam.nearPlane) * depthRangeInv;
                    depth = std::clamp(depth, 0.0f, 1.0f);

                    uint16_t pipelineId = store.pipelineId[i];
                    if (pipelineId == 0 && store.pipelines[i].IsValid())
                        pipelineId = store.pipelines[i].GetIndex();

                    RHI::DrawSortKey sortKey = RHI::CreateSortKey(pass, 0, pipelineId, store.materialId[i], depth);
                    uint8_t geometryId = getOrAddGeometry(store.vertexBuffers[i], store.indexBuffers[i]);

                    RHI::DrawPacket packet{};
                    packet.sortKey = sortKey;
                    packet.transformIndex = baseTransformIndex + store.transformIndex[i];
                    packet.geometryId = geometryId;
                    packet.cmdType = static_cast<uint8_t>(RHI::DrawCmdType::Indexed);

                    packet.indexed.indexCount = store.indexCount[i];
                    packet.indexed.firstIndex = store.firstIndex[i];
                    packet.indexed.vertexOffset = store.vertexOffset[i];

                    frame->drawPackets.push_back(packet);
                }
            }
            else if (path == RenderPath::GPUDriven)
            {
                const auto& gpuBatch = scene->m_impl->gpuBatch;
                if (gpuBatch.enabled && gpuBatch.maxObjects > 0)
                {
                    frame->gpuDriven = gpuBatch;

                    uint8_t geometryId = getOrAddGeometry(gpuBatch.vertexBuffer, gpuBatch.indexBuffer);
                    uint16_t pipelineId = gpuBatch.renderPipeline.IsValid() ? gpuBatch.renderPipeline.GetIndex() : 0;
                    RHI::DrawSortKey sortKey = RHI::CreateSortKey(RHI::RenderPass::Opaque, 0, pipelineId, 0, 0.0f);

                    RHI::DrawPacket packet{};
                    packet.sortKey = sortKey;
                    packet.geometryId = geometryId;
                    packet.cmdType = static_cast<uint8_t>(RHI::DrawCmdType::MultiDrawIndirectCount);

                    packet.mdiCount.indirectBufferId = gpuBatch.indirectBuffer.GetIndex();
                    packet.mdiCount.countBufferId = gpuBatch.countBuffer.GetIndex();
                    packet.mdiCount.indirectOffset = 0;
                    packet.mdiCount.countOffset = 0;
                    packet.mdiCount.maxDrawCount = gpuBatch.maxObjects;

                    frame->drawPackets.push_back(packet);
                }
            }

            // Lights (both paths)
            for (uint32_t i = 0; i < static_cast<uint32_t>(scene->m_impl->lights.dirLights.size()); ++i)
            {
                const auto& l = scene->m_impl->lights.dirLights[i];
                frame->dirLights.push_back({ l.direction, l.intensity, l.color, 0.f });
            }
            for (uint32_t i = 0; i < static_cast<uint32_t>(scene->m_impl->lights.pointLights.size()); ++i)
            {
                const auto& l = scene->m_impl->lights.pointLights[i];
                frame->pointLights.push_back({ l.position, l.radius, l.color, l.intensity });
            }

            ++sceneIdx;
        }

        return frame;
    }
}
