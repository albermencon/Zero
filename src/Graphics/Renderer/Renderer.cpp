#include "pch.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/BackendFactory/GraphicsBackendFactory.h"
#include <Engine/Log.h>

namespace Zero
{
	static bool initialized = false;

	Renderer& Renderer::Get()
	{
		static Renderer instance;
		return instance;
	}

	Renderer::Renderer()
	{
	}

	Renderer::~Renderer()
	{
	}

	void Renderer::Init(RHI::API api, Window* window)
	{
		ENGINE_CORE_ASSERT(!m_initialized.load(), "Renderer already initialized");
		m_API = api;
		m_window = window;

		m_backend = GraphicsBackend::Create(BackendType::Vulkan, *window);
		ENGINE_CORE_ASSERT(m_backend, "Failed to create RenderBackend");

		// 
		m_running.store(true, std::memory_order_release);

		// Backend must exist before the thread starts
		m_renderThread = Thread(&Renderer::RenderLoop, this);
		m_initialized.store(true);
		ENGINE_CORE_INFO("Renderer initialized");
	}

	void Renderer::Shutdown()
	{
		if (!m_initialized.load(std::memory_order_acquire)) return;

		m_running.store(false, std::memory_order_release);

		if (m_renderThread.Joinable())
			m_renderThread.Join();

		if (m_backend)
			m_backend->OnFinished();

		m_backend.reset();
		m_initialized.store(false, std::memory_order_release);
		ENGINE_CORE_WARN("Renderer shut down");
	}

	void Renderer::OnRenderSurfaceResize()
	{
		m_backend->OnRenderSurfaceResize();
	}

	void Renderer::RequestFrame()
	{
		m_queue.Adquire();
	}

	void Renderer::SubmitFrame(FrameData* frame)
	{
		(void)m_queue.Push(frame);
	}

	void Renderer::RenderLoop()
	{
		m_renderThread.SetName("RendererThread");
		m_renderThread.SetPriority(ThreadPriority::High);
		m_renderThread.SetAffinity(1ull << 1); // temp pinned to core 1
		ENGINE_CORE_INFO("Initialized render thread");

		size_t idleCount = 0;
		while (m_running.load())
		{
			FrameData* frame = nullptr;
			if (!m_queue.Pop(frame))
			{
				if (idleCount < 50) 
					Thread::Yield();
				else 
					Thread::Sleep(1); // Sleep 1 ms
				++idleCount;
				continue;
			}
			idleCount = 0;

			// Just drawing a triangule temp
			m_backend->BeginFrame();
			m_backend->EndFrame();
			m_backend->SwapBuffers();

			// 1. Wait for frame
			// 2. Consume from FrameQueue
			// 3. Submit to GPU
			// 4. Call OnFrameCompleted()

			Thread::Yield();
			m_queue.FrameConsumed();
		}
	}
}
