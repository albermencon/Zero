#include "pch.h"
#include <Engine/Window.h>
#include <Engine/Log.h>
#include <Engine/Thread/ScopedLock.h>
#include <Engine/Profiler/Profiler.h>
#include <chrono>
#include <semaphore>
#include <algorithm>
#include "Graphics/core/GraphicsCore.h"
#include "Graphics/BackendFactory/GraphicsBackendFactory.h"
#include "Graphics/Renderer/RenderInterfaceImpl.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/core/Pipeline.h"

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

	void Renderer::WaitIdle()
	{
		if (!m_initialized.load(std::memory_order_acquire) || !m_running.load(std::memory_order_acquire))
			return;

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
			m_queue.Adquire();
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
			m_queue.FrameConsumed();

		std::binary_semaphore sem(0);
		SubmitRenderCommand([this, &sem]() {
			if (m_backend)
				m_backend->OnFinished();
			sem.release();
		});
		sem.acquire();
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

	void Renderer::SubmitRenderCommand(std::move_only_function<void()> cmd)
	{
		Zero::ScopedLock lock(m_CommandMutex);
		m_RenderCommands.push_back(std::move(cmd));
	}

	void Renderer::ExecuteRenderCommands()
	{
		std::vector<std::move_only_function<void()>> localCommands;
		{
			Zero::ScopedLock lock(m_CommandMutex);
			localCommands.swap(m_RenderCommands);
		}
		for (auto& cmd : localCommands)
		{
			cmd();
		}
	}

	void Renderer::InitImGui()
	{
		std::binary_semaphore sem(0);
		SubmitRenderCommand([this, &sem]() {
			m_backend->InitImGui();
			sem.release();
		});
		sem.acquire();
	}

	void Renderer::ShutdownImGui()
	{
		std::binary_semaphore sem(0);
		SubmitRenderCommand([this, &sem]() {
			m_backend->ShutdownImGui();
			sem.release();
		});
		sem.acquire();
	}

	void Renderer::UpdateImGuiTextures()
	{
		std::binary_semaphore sem(0);
		SubmitRenderCommand([this, &sem]() {
			m_backend->UpdateImGuiTextures();
			sem.release();
		});
		sem.acquire();
	}

	void Renderer::RenderLoop()
	{
		m_renderThread.SetName("RendererThread");
		m_renderThread.SetPriority(ThreadPriority::High);
		m_renderThread.SetAffinity(1ull << 1); // temp pinned to core 1
		ZERO_CORE_INFO("Initialized render thread");

		ZERO_PROFILE_THREAD("RendererThread");
		ZERO_PROFILE_FUNCTION();
		size_t idleCount = 0;
#ifdef ZERO_ENABLE_PROFILING
		using Clock = std::chrono::steady_clock;
		auto elapsedMs = [](Clock::time_point start, Clock::time_point end) -> float {
			return std::chrono::duration<float, std::milli>(end - start).count();
		};
#endif
		while (m_running.load())
		{
			ExecuteRenderCommands();

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

#ifdef ZERO_ENABLE_PROFILING
			const auto tFrameStart = Clock::now();
			const bool frameBegan = m_backend->BeginFrame();
			const auto tAfterBegin = Clock::now();
			if (!frameBegan)
#else
			if (!m_backend->BeginFrame())
#endif
			{
				m_queue.FrameConsumed();
				Thread::Sleep(1); // Yield while the backend is unready
				continue;
			}

			ProcessResourceRequests();

			if (frame && !frame->drawPackets.empty())
			{
				std::sort(frame->drawPackets.begin(), frame->drawPackets.end(),
					[](const RHI::DrawPacket& a, const RHI::DrawPacket& b) noexcept {
						return a.sortKey < b.sortKey;
					});
			}

#ifdef ZERO_ENABLE_PROFILING
			const auto tBeforeTransfers = Clock::now();
			m_backend->FlushTransfers();
			const auto tBeforeRender = Clock::now();
			m_backend->RenderFrame(frame);
			const auto tBeforeSwap = Clock::now();
			m_backend->SwapBuffers();
			const auto tFrameEnd = Clock::now();

			RHI::FrameTimingStats stats = m_backend->GetLastFrameTimings();
			stats.frameIndex = ++m_completedFrameCount;
			stats.cpuBeginFrameMs = elapsedMs(tFrameStart, tAfterBegin);
			stats.cpuTransfersMs = elapsedMs(tBeforeTransfers, tBeforeRender);
			stats.cpuRenderRecordMs = elapsedMs(tBeforeRender, tBeforeSwap);
			stats.cpuSwapPresentMs = elapsedMs(tBeforeSwap, tFrameEnd);
			stats.cpuRenderTotalMs = elapsedMs(tFrameStart, tFrameEnd);

			{
				Zero::ScopedLock lock(m_timingsMutex);
				m_latestTimings = stats;
			}
#else
			m_backend->FlushTransfers();
			m_backend->RenderFrame(frame);
			m_backend->SwapBuffers();
#endif

			// 1. Wait for frame
			// 2. Consume from FrameQueue
			// 3. Submit to GPU
			// 4. Call OnFrameCompleted()

			Thread::Yield();
			m_queue.FrameConsumed();
			ZERO_PROFILE_FRAME_NAME("Render");
		}
	}

	RHI::FrameTimingStats Renderer::GetFrameTimings() const
	{
#ifdef ZERO_ENABLE_PROFILING
		Zero::ScopedLock lock(m_timingsMutex);
		return m_latestTimings;
#else
		return {};
#endif
	}

	void Renderer::ProcessResourceRequests()
	{
		auto& impl = RenderInterfaceImpl::Get();

		static std::vector<BufferCreateRequest> localBuffers;
		static std::vector<BufferUpdateRequest> localBufferUpdates;
		static std::vector<TextureCreateRequest> localTextures;
		static std::vector<PipelineCreateRequest> localPipelines;
		static std::vector<ComputePipelineCreateRequest> localComputePipelines;
		static std::vector<ResourceDestroyRequest> localDestroys;

		{
			Zero::ScopedLock lock(impl.queueMutex);
			localBuffers.swap(impl.pendingBuffers);
			localBufferUpdates.swap(impl.pendingBufferUpdates);
			localTextures.swap(impl.pendingTextures);
			localPipelines.swap(impl.pendingPipelines);
			localComputePipelines.swap(impl.pendingComputePipelines);
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

		// Process Buffer Updates
		for (const auto& req : localBufferUpdates)
		{
			Buffer* buffer = impl.bufferRegistry.GetBackendResource(req.handle);
			if (buffer)
			{
				m_backend->UpdateBuffer(buffer, req.dstOffset, req.data.data(), req.data.size());
			}
		}

		// Process Texture Creations
		for (const auto& req : localTextures)
		{
			impl.textureRegistry.MarkReady(req.handle, nullptr);
		}

		// Process Graphics Pipeline Creations
		for (auto& req : localPipelines)
		{
#if defined(ZERO_DEBUG) || defined(ZERO_ENABLE_PROFILING)
			req.desc.debugName = req.ownedDebugName.empty() ? nullptr : req.ownedDebugName.c_str();
#endif
			GraphicsPipeline* pipeline = m_backend->CreateGraphicsPipeline(req.desc);
			if (pipeline)
			{
				Pipeline* basePipeline = pipeline;
				impl.pipelineRegistry.MarkReady(req.handle, basePipeline);
			}
			else
			{
				ZERO_CORE_ERROR("Failed to create graphics pipeline for handle {0}", req.handle.GetId());
				impl.pipelineRegistry.MarkFailed(req.handle);
			}
		}

		// Process Compute Pipeline Creations
		for (auto& req : localComputePipelines)
		{
#if defined(ZERO_DEBUG) || defined(ZERO_ENABLE_PROFILING)
			req.desc.debugName = req.ownedDebugName.empty() ? nullptr : req.ownedDebugName.c_str();
#endif
			ComputePipeline* pipeline = m_backend->CreateComputePipeline(req.desc);
			if (pipeline)
			{
				Pipeline* basePipeline = pipeline;
				impl.pipelineRegistry.MarkReady(req.handle, basePipeline);
			}
			else
			{
				ZERO_CORE_ERROR("Failed to create compute pipeline for handle {0}", req.handle.GetId());
				impl.pipelineRegistry.MarkFailed(req.handle);
			}
		}

		for (const auto& req : localDestroys)
		{
			m_deferredDestroys.push_back({ req.handleValue, static_cast<uint32_t>(req.type), MAX_FRAMES_IN_FLIGHT });
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
				Pipeline* pipeline = impl.pipelineRegistry.GetBackendResource(handle);
				if (pipeline)
				{
					if (pipeline->IsCompute())
						m_backend->DestroyComputePipeline(static_cast<ComputePipeline*>(pipeline));
					else
						m_backend->DestroyGraphicsPipeline(static_cast<GraphicsPipeline*>(pipeline));
				}
				impl.pipelineRegistry.ReclaimSlot(handle);
			}

			it = m_deferredDestroys.erase(it);
		}

		localBuffers.clear();
		localBufferUpdates.clear();
		localTextures.clear();
		localPipelines.clear();
		localComputePipelines.clear();
		localDestroys.clear();
	}
}
