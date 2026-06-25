#include "Engine/Window.h"
#include "pch.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/BackendFactory/GraphicsBackendFactory.h"
#include "Graphics/Renderer/RenderInterfaceImpl.h"
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
		ZERO_CORE_ASSERT(!m_initialized.load(), "Renderer already initialized");
		m_API = api;
		m_window = window;

		m_backend = GraphicsBackend::Create(api, *window);
		ZERO_CORE_ASSERT(m_backend, "Failed to create RenderBackend");

		// 
		m_running.store(true, std::memory_order_release);

		// Backend must exist before the thread starts
		m_renderThread = Thread(&Renderer::RenderLoop, this);
		m_initialized.store(true);
		ZERO_CORE_INFO("Renderer initialized");
	}

	void Renderer::Shutdown()
	{
		if (!m_initialized.load(std::memory_order_acquire)) return;

		m_running.store(false, std::memory_order_release);

		if (m_renderThread.Joinable())
			m_renderThread.Join();

		if (m_backend)
		{
			m_backend->OnFinished();

			auto& impl = RenderInterfaceImpl::Get();
			
			impl.bufferRegistry.ForEach([this](BufferHandle h, Buffer* b) {
				if (b) m_backend->DestroyBuffer(b);
			});
			impl.textureRegistry.ForEach([this](TextureHandle h, Texture* t) {
				// if (t) m_backend->DestroyTexture(t);
			});
			impl.pipelineRegistry.ForEach([this](PipelineHandle h, Pipeline* p) {
				// if (p) m_backend->DestroyPipeline(p);
			});

			impl.Clear();
			m_deferredDestroys.clear();
		}

		m_backend.reset();
		m_initialized.store(false, std::memory_order_release);
		ZERO_CORE_WARN("Renderer shut down");
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
		ZERO_CORE_INFO("Initialized render thread");

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

			m_backend->BeginFrame();

			ProcessResourceRequests();

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

	void Renderer::ProcessResourceRequests()
	{
		auto& impl = RenderInterfaceImpl::Get();

		static std::vector<BufferCreateRequest> localBuffers;
		static std::vector<TextureCreateRequest> localTextures;
		static std::vector<PipelineCreateRequest> localPipelines;
		static std::vector<ResourceDestroyRequest> localDestroys;

		{
			std::lock_guard<std::mutex> lock(impl.queueMutex);
			localBuffers.swap(impl.pendingBuffers);
			localTextures.swap(impl.pendingTextures);
			localPipelines.swap(impl.pendingPipelines);
			localDestroys.swap(impl.pendingDestroys);
		}

		// Process Buffer Creations
		for (auto& req : localBuffers)
		{
			if (!req.ownedInitialData.empty())
			{
				req.desc.initialData = req.ownedInitialData.data();
				req.desc.initialDataSize = req.ownedInitialData.size();
			}
			Buffer* buffer = m_backend->CreateBuffer(req.desc);
			if (buffer)
			{
				impl.bufferRegistry.MarkReady(req.handle, buffer);
			}
			else
			{
				ZERO_CORE_ERROR("Failed to create buffer for handle {0}", req.handle.GetId());
				impl.bufferRegistry.MarkFailed(req.handle);
			}
		}

		// Process Texture Creations
		for (const auto& req : localTextures)
		{
			impl.textureRegistry.MarkReady(req.handle, nullptr);
		}

		// Process Pipeline Creations
		for (const auto& req : localPipelines)
		{
			impl.pipelineRegistry.MarkReady(req.handle, nullptr);
		}

		for (const auto& req : localDestroys)
		{
			m_deferredDestroys.push_back({ req.handleValue, static_cast<uint32_t>(req.type), 2 }); // MAX_FRAMES_IN_FLIGHT = 2
		}

		for (auto it = m_deferredDestroys.begin(); it != m_deferredDestroys.end(); )
		{
			if (it->frameCountdown > 0)
			{
				it->frameCountdown--;
				++it;
				continue;
			}

			if (it->type == static_cast<uint32_t>(ResourceDestroyRequest::Type::Buffer))
			{
				BufferHandle handle{ it->handleValue };
				Buffer* buffer = impl.bufferRegistry.GetBackendResource(handle);
				if (buffer) m_backend->DestroyBuffer(buffer);
				impl.bufferRegistry.ReclaimSlot(handle);
			}
			else if (it->type == static_cast<uint32_t>(ResourceDestroyRequest::Type::Texture))
			{
				TextureHandle handle{ it->handleValue };
				impl.textureRegistry.ReclaimSlot(handle);
			}
			else if (it->type == static_cast<uint32_t>(ResourceDestroyRequest::Type::Pipeline))
			{
				PipelineHandle handle{ it->handleValue };
				impl.pipelineRegistry.ReclaimSlot(handle);
			}

			it = m_deferredDestroys.erase(it);
		}

		localBuffers.clear();
		localTextures.clear();
		localPipelines.clear();
		localDestroys.clear();
	}
}
