#include "pch.h"
#include <Engine/Scene/Scene.h>
#include <Engine/Scene/SceneTypes.h>
#include "SceneCommand.h"
#include "SceneData.h"
#include <concurrentqueue.h>

namespace Zero
{
    Scene::Scene(uint32_t id, std::string_view name, RenderPath path)
        : m_impl(new Impl(id, name, path))
    {
    }

    Scene::~Scene() { delete m_impl; }

    std::string_view Scene::GetName() const noexcept { return m_impl->name; }
    SceneId          Scene::GetId()   const noexcept { return m_impl->id; }

    RenderableHandle Scene::AddRenderable(const RenderableDesc& desc)
    {
        const uint32_t id = m_impl->nextHandleId.fetch_add(1, std::memory_order_relaxed);

        SceneCommand cmd;
        cmd.type = SceneCommandType::AddRenderable;
        cmd.handleId = id;

        cmd.renderable.boundsMin = desc.boundsMin;
        cmd.renderable.boundsMax = desc.boundsMax;
        cmd.renderable.transform = desc.transform;
        cmd.renderable.vertexBufferId = desc.vertexBuffer.GetId();
        cmd.renderable.indexBufferId = desc.indexBuffer.GetId();
        cmd.renderable.pipelineId_ = desc.pipeline.GetId();
        cmd.renderable.indexCount = desc.indexCount;
        cmd.renderable.firstIndex = desc.firstIndex;
        cmd.renderable.vertexOffset = desc.vertexOffset;
        cmd.renderable.materialId = desc.materialId;
        cmd.renderable.pipelineId = desc.pipelineId;
        cmd.renderable.castsShadow = desc.castsShadow;
        cmd.renderable.isTransparent = desc.isTransparent;
        
        m_impl->commands.enqueue(cmd);

        RenderableHandle h;
        h.id = id;
        return h;
    }

    void Scene::RemoveRenderable(RenderableHandle handle)
    {
        SceneCommand cmd;
        cmd.type = SceneCommandType::RemoveRenderable;
        cmd.handleId = handle.id;
        m_impl->commands.enqueue(cmd);
    }

    void Scene::UpdateTransform(RenderableHandle handle, const Mat4& transform)
    {
        SceneCommand cmd;
        cmd.type = SceneCommandType::UpdateTransform;
        cmd.handleId = handle.id;
        cmd.updateTransform.transform = transform;
        m_impl->commands.enqueue(cmd);
    }

    void Scene::UpdateBounds(RenderableHandle handle, const Vec3& min, const Vec3& max)
    {
        SceneCommand cmd;
        cmd.type = SceneCommandType::UpdateBounds;
        cmd.handleId = handle.id;
        cmd.updateBounds.min = min;
        cmd.updateBounds.max = max;
        m_impl->commands.enqueue(cmd);
    }

    LightHandle Scene::AddDirectionalLight(const DirectionalLightDesc& desc)
    {
        const uint32_t id = m_impl->nextHandleId.fetch_add(1, std::memory_order_relaxed);
        SceneCommand cmd;
        cmd.type = SceneCommandType::AddDirectionalLight;
        cmd.handleId = id;
        cmd.dirLight = desc;
        m_impl->commands.enqueue(cmd);
        LightHandle h; h.id = id; return h;
    }

    LightHandle Scene::AddPointLight(const PointLightDesc& desc)
    {
        const uint32_t id = m_impl->nextHandleId.fetch_add(1, std::memory_order_relaxed);
        SceneCommand cmd;
        cmd.type = SceneCommandType::AddPointLight;
        cmd.handleId = id;
        cmd.pointLight = desc;
        m_impl->commands.enqueue(cmd);
        LightHandle h; h.id = id; return h;
    }

    void Scene::RemoveLight(LightHandle handle)
    {
        SceneCommand cmd;
        cmd.type = SceneCommandType::RemoveLight;
        cmd.handleId = handle.id;
        m_impl->commands.enqueue(cmd);
    }

    void Scene::UpdatePointLightPosition(LightHandle handle, const Vec3& position)
    {
        SceneCommand cmd;
        cmd.type = SceneCommandType::UpdatePointLightPosition;
        cmd.handleId = handle.id;
        cmd.updatePointLight.position = position;
        m_impl->commands.enqueue(cmd);
    }

    void Scene::SetCamera(const CameraDesc& desc)
    {
        SceneCommand cmd;
        cmd.type = SceneCommandType::SetCamera;
        cmd.camera = desc;
        m_impl->commands.enqueue(cmd);
    }

    // Scene internal — called only by SceneManager (friend)

    void Scene_FlushCommands(Scene& scene)
    {
        auto& impl = *scene.m_impl;
        SceneCommand cmd;

        while (impl.commands.try_dequeue(cmd))
        {
            switch (cmd.type)
            {
            case SceneCommandType::AddRenderable:
                impl.renderables.Insert(cmd.handleId, cmd.renderable);
                break;

            case SceneCommandType::RemoveRenderable:
                impl.renderables.Remove(cmd.handleId);
                break;

            case SceneCommandType::UpdateTransform:
                impl.renderables.UpdateTransform(cmd.handleId, cmd.updateTransform.transform);
                break;

            case SceneCommandType::UpdateBounds:
                impl.renderables.UpdateBounds(cmd.handleId, cmd.updateBounds.min, cmd.updateBounds.max);
                break;

            case SceneCommandType::AddDirectionalLight:
                impl.lights.AddDirectionalLight(cmd.handleId, cmd.dirLight);
                impl.lightTypeMap[cmd.handleId] = true;
                break;

            case SceneCommandType::AddPointLight:
                impl.lights.AddPointLight(cmd.handleId, cmd.pointLight);
                impl.lightTypeMap[cmd.handleId] = false;
                break;

            case SceneCommandType::RemoveLight:
            {
                auto it = impl.lightTypeMap.find(cmd.handleId);
                if (it != impl.lightTypeMap.end())
                {
                    if (it->second)
                        impl.lights.RemoveDirectionalLight(cmd.handleId);
                    else
                        impl.lights.RemovePointLight(cmd.handleId);
                    impl.lightTypeMap.erase(it);
                }
                break;
            }

            case SceneCommandType::UpdatePointLightPosition:
                impl.lights.UpdatePointLightPosition(cmd.handleId, cmd.updatePointLight.position);
                break;

            case SceneCommandType::SetCamera:
                impl.camera = cmd.camera;
                break;
            }
        }
    }
}
