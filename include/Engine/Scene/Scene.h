#pragma once
#include <cstdint>
#include <string_view>
#include <Engine/Core.h>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Scene/SceneTypes.h>

namespace Zero
{
    struct CameraDesc
    {
        Mat4  view;
        Mat4  proj;
        Vec3  position;
        float nearPlane = 0.1f;
        float farPlane = 1000.f;
    };

    // Scene
    //   All mutating calls are thread-safe — internally enqueued into a
    //   lock-free MPSC queue, drained by the engine each frame.
    //   Changes are visible the following frame.
    class ENGINE_API Scene
    {
    public:
        ~Scene();  // public — unique_ptr needs access    

        // Renderables 

        [[nodiscard]] RenderableHandle AddRenderable(const RenderableDesc& desc);
        void RemoveRenderable(RenderableHandle handle);
        void UpdateTransform(RenderableHandle handle, const Mat4& transform);
        void UpdateBounds(RenderableHandle handle, const Vec3& min, const Vec3& max);

        // Lights 

        [[nodiscard]] LightHandle AddDirectionalLight(const DirectionalLightDesc& desc);
        [[nodiscard]] LightHandle AddPointLight(const PointLightDesc& desc);
        void RemoveLight(LightHandle handle);
        void UpdatePointLightPosition(LightHandle handle, const Vec3& position);

        // Camera 

        void SetCamera(const CameraDesc& desc);

        // Query 

        [[nodiscard]] std::string_view GetName() const noexcept;
        [[nodiscard]] SceneId          GetId()   const noexcept;

    private:
        friend class SceneManager;
        friend void Scene_FlushCommands(Scene& scene);

        explicit Scene(uint32_t id, std::string_view name, RenderPath path);

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&) = delete;
        Scene& operator=(Scene&&) = delete;

        struct Impl;
        Impl* m_impl = nullptr;
    };
}
