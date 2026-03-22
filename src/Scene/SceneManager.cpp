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
        m_impl->scenes.emplace_back(new Scene(MainSceneId, "Main"));
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

    SceneId SceneManager::CreateScene(std::string_view name)
    {
        SceneId id = m_impl->nextSceneId++;
        m_impl->scenes.emplace_back(new Scene(id, name));
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

        uint32_t total = 0;
        for (SceneId id : m_impl->activeIds)
            if (Scene* s = GetScene(id)) total += s->m_impl->renderables.Count();

        if (total != 0)
        {
            ENGINE_CORE_TRACE("Total renderables: {}", total);
            frame->Reserve(total);
        }

        uint32_t offset = 0;
        uint32_t sceneIdx = 0;

        for (SceneId id : m_impl->activeIds)
        {
            if (sceneIdx >= MaxActiveScenes) break;
            Scene* scene = GetScene(id);
            if (!scene) continue;

            auto& store = scene->m_impl->renderables;
            const uint32_t count = store.Count();

            frame->sceneRanges[sceneIdx] = { offset, count, sceneIdx };
            frame->cameras[sceneIdx] = scene->m_impl->camera;
            frame->sceneCount = sceneIdx + 1;

            if (count > 0)
            {
                ENGINE_CORE_TRACE("Ok");
                AppendArray(frame->renderables.boundsMinX, store.boundsMinX, offset);
                AppendArray(frame->renderables.boundsMinY, store.boundsMinY, offset);
                AppendArray(frame->renderables.boundsMinZ, store.boundsMinZ, offset);
                AppendArray(frame->renderables.boundsMaxX, store.boundsMaxX, offset);
                AppendArray(frame->renderables.boundsMaxY, store.boundsMaxY, offset);
                AppendArray(frame->renderables.boundsMaxZ, store.boundsMaxZ, offset);

                AppendArray(frame->renderables.sortKeys, store.sortKeys, offset);
                AppendArray(frame->renderables.transformIndex, store.transformIndex, offset);

                AppendArray(frame->renderables.vertexBuffers, store.vertexBuffers, offset);
                AppendArray(frame->renderables.indexBuffers, store.indexBuffers, offset);
                AppendArray(frame->renderables.pipelines, store.pipelines, offset);
                AppendArray(frame->renderables.indexCount, store.indexCount, offset);
                AppendArray(frame->renderables.firstIndex, store.firstIndex, offset);
                AppendArray(frame->renderables.vertexOffset, store.vertexOffset, offset);

                // Transforms append after existing — offset is current size
                const uint32_t tOffset = static_cast<uint32_t>(
                    frame->renderables.transforms.size());
                AppendArray(frame->renderables.transforms, store.transforms, tOffset);

                // Lights
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

                offset += count;
            }

            ++sceneIdx;
        }

        return frame;
    }
}
