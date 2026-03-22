#pragma once
#include <cstdint>
#include <string_view>
#include <Engine/Core.h>
#include <Engine/Scene/SceneTypes.h>

namespace Zero
{
    class Scene;
    class Application;

    using SceneId = uint32_t;
    //static constexpr SceneId MainSceneId = 0;
    //static constexpr uint32_t MaxActiveScenes = 8;

    // SceneManager
    //   Owns all Scene instances. Singleton, accessible from any thread.
    //
    //   The main scene (MainSceneId) is always available — no setup needed.
    //   Additional scenes can be created, activated, and destroyed at will.
    //
    //   All scene mutation calls (via Scene API) are thread-safe.
    class ENGINE_API SceneManager
    {
    public:
        static SceneManager& Get();

        // Scene management (public, any thread)

        // Creates a new scene. Returns its stable id.
        [[nodiscard]] SceneId CreateScene(std::string_view name);

        // Always valid. MainSceneId guaranteed to exist.
        [[nodiscard]] Scene* GetScene(SceneId id);
        [[nodiscard]] Scene* GetMainScene();

        // Only active scenes are snapshotted each frame.
        void SetSceneActive(SceneId id, bool active);

        // MainScene cannot be destroyed.
        void DestroyScene(SceneId id);

    private:
        // Internal lifecycle — called only by Application
        friend class Application;

        void Init();
        void Shutdown();
        void FlushCommands();
        [[nodiscard]] struct FrameData* BuildFrame(uint64_t frameIndex, float deltaTime);

        SceneManager() = default;
        ~SceneManager() = default;
        SceneManager(const SceneManager&) = delete;
        SceneManager& operator=(const SceneManager&) = delete;

        struct Impl;
        Impl* m_impl = nullptr;
    };
}
