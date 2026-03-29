#pragma once
#include <cstdint>
#include <memory>
#include <Engine/Core.h>
#include "Graphics/Renderer/core/Framequeue.h"
#include <Engine/Thread/Thread.h>
#include <Engine/Window.h>
#include "Graphics/core/RHITypes.h"

namespace Zero
{
	class GraphicsDevice;

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

		void OnRenderSurfaceResize();

		// Called from main thread
		void RequestFrame();

		void SubmitFrame(FrameData* frame);

	private:
		void RenderLoop();

	private:
		std::unique_ptr<class GraphicsDevice> m_backend;
		Window* m_window = nullptr;
		RHI::API m_API = RHI::API::Vulkan;

		FrameQueue m_queue;
		Thread m_renderThread;
		std::atomic<bool> m_running{false};
		std::atomic<bool> m_initialized{ false };
	};
}
