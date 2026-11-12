#pragma once
#include <cstdint>
#include <memory>
#include <Engine/Core.h>
#include "Graphics/Renderer/core/Framequeue.h"
#include <Engine/Thread/Thread.h>
#include <Engine/Thread/Mutex.h>
#include <functional>
#include <vector>
#include <Engine/Window.h>
#include <Engine/Graphics/RHITypes.h>

namespace Zero
{
	class GraphicsDevice;
	class Buffer;

	class Renderer
	{
	private:
		Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;
	public:
		~Renderer();

		static Renderer& Get();

		void Init(RHI::API api, Window* window);
		void Shutdown();
		void WaitIdle();

		void InitImGui();
		void ShutdownImGui();

		// Uploads pending Dear ImGui texture updates synchronously on the render
		// thread (blocks until done). Safe to call from the main thread.
		void UpdateImGuiTextures();

		void OnRenderSurfaceResize();

		// Called from main thread
		void RequestFrame();

		void SubmitFrame(FrameData* frame);

		void SubmitRenderCommand(std::move_only_function<void()> cmd);

	private:
		void RenderLoop();
		void ProcessResourceRequests();
		void ExecuteRenderCommands();

		void CreateStagingBuffer();

	private:
		Zero::Mutex m_CommandMutex;
		std::vector<std::move_only_function<void()>> m_RenderCommands;
		std::unique_ptr<class GraphicsDevice> m_backend;
		Window* m_window = nullptr;
		RHI::API m_API = RHI::API::Vulkan;

		FrameQueue m_queue;
		Thread m_renderThread;
		std::atomic<bool> m_running{false};
		std::atomic<bool> m_initialized{ false };

		// Staging buffer for updates
		Buffer* m_stagingBuffer = nullptr;
		size_t m_stagingBufferOffset = 0;

		struct DeferredDestroy {
			uint32_t handleValue;
			uint32_t type; // casted from ResourceDestroyRequest::Type
			uint32_t frameCountdown;
		};
		std::vector<DeferredDestroy> m_deferredDestroys;
	};
}
