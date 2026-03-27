#include "pch.h"
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Log.h>
#include "Scene/SceneData.h"
#include "Graphics/core/FrameData.h"
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
    };

    SceneManager& SceneManager::Get()
    {
        static SceneManager instance;
        return instance;
    }

    void SceneManager::Init()
    {
        m_impl = new Impl();
        m_impl->scenes.emplace_back(new Scene(MainSceneId, "Main", RenderPath::CPUDriven));
        m_impl->activeIds.push_back(MainSceneId);
        ENGINE_CORE_INFO("SceneManager initialized.");
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
                ENGINE_CORE_WARN("MaxActiveScenes reached, cannot activate scene {0}", id);
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

    FrameData* SceneManager::BuildFrame(uint64_t frameIndex, float deltaTime)
    {
        auto* frame = new FrameData();
        frame->frameIndex = frameIndex;
        frame->deltaTime = deltaTime;

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
        frame->Reserve(cpuDrawCount, cpuTransformCount, gpuBatchCount);

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

            frame->sceneRanges[sceneIdx] = { cpuOffset, count, sceneIdx, path };
            frame->cameras[sceneIdx] = scene->m_impl->camera;
            frame->sceneCount = sceneIdx + 1;

            if (path == RenderPath::CPUDriven && count > 0)
            {
                // memcpy HOT
                AppendArray(frame->cpuDriven.boundsMinX, store.boundsMinX, cpuOffset);
                AppendArray(frame->cpuDriven.boundsMinY, store.boundsMinY, cpuOffset);
                AppendArray(frame->cpuDriven.boundsMinZ, store.boundsMinZ, cpuOffset);
                AppendArray(frame->cpuDriven.boundsMaxX, store.boundsMaxX, cpuOffset);
                AppendArray(frame->cpuDriven.boundsMaxY, store.boundsMaxY, cpuOffset);
                AppendArray(frame->cpuDriven.boundsMaxZ, store.boundsMaxZ, cpuOffset);

                // memcpy WARM
                AppendArray(frame->cpuDriven.sortKeys, store.sortKeys, cpuOffset);
                AppendArray(frame->cpuDriven.transformIndex, store.transformIndex, cpuOffset);

                // memcpy COLD
                AppendArray(frame->cpuDriven.vertexBuffers, store.vertexBuffers, cpuOffset);
                AppendArray(frame->cpuDriven.indexBuffers, store.indexBuffers, cpuOffset);
                AppendArray(frame->cpuDriven.indexCount, store.indexCount, cpuOffset);
                AppendArray(frame->cpuDriven.firstIndex, store.firstIndex, cpuOffset);
                AppendArray(frame->cpuDriven.vertexOffset, store.vertexOffset, cpuOffset);
                AppendArray(frame->cpuDriven.materialId, store.materialId, cpuOffset);
                AppendArray(frame->cpuDriven.castsShadow, store.castsShadow, cpuOffset);
                AppendArray(frame->cpuDriven.isTransparent, store.isTransparent, cpuOffset);

                const uint32_t tOffset = frame->cpuDriven.TransformCount();
                AppendArray(frame->cpuDriven.transforms, store.transforms, tOffset);

                cpuOffset += count;
            }
            else if (path == RenderPath::GPUDriven)
            {
                // GPU driven — just copy the batch handles, no SOA iteration
                // SceneManager only forwards what the scene has
                //frame->gpuDriven = scene->m_impl->gpuBatch;
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
