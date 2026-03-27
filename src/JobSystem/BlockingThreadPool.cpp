#include "BlockingThreadPool.h"
#include <Engine/Log.h>

namespace Zero
{
	inline std::atomic<bool> initialized{ false };

	BlockingThreadPool::BlockingThreadPool()
	{
	}

	void BlockingThreadPool::Init(uint32_t threadCount)
	{
		bool expected = false;
		if (!initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			return;

		uint32_t hc = std::thread::hardware_concurrency();
		if (hc == 0) hc = 1;

		if (threadCount == 0 || threadCount > hc)
		{
			threadCount = (hc > 1) ? (hc - 1) : 1;
		}

		m_threads = new Thread[threadCount];
		m_count = threadCount;
		m_running.store(true, std::memory_order_release);
		for (uint32_t i = 0; i < threadCount; ++i)
		{
			m_threads[i] = Thread(&BlockingThreadPool::worker_loop, this, i);
		}
	}

	static constexpr int kStop = -1;
	void BlockingThreadPool::Shutdown()
	{
		if (!initialized.load(std::memory_order_acquire))
			return;

		m_running.store(false, std::memory_order_release);
		
		// poison pill
		for (uint32_t i = 0; i < m_count; ++i)
		{
			m_queue.enqueue(kStop);
		}

		for (uint32_t i = 0; i < m_count; ++i)
		{
			Thread& thread = m_threads[i];
			thread.Join();
		}

		delete[] m_threads;
		m_threads = nullptr;
		m_count = 0;
		initialized.store(false, std::memory_order_release);
	}

	BlockingThreadPool& BlockingThreadPool::Get()
	{
		static BlockingThreadPool instance;
		return instance;
	}

	void BlockingThreadPool::worker_loop(uint32_t threadID)
	{
		// temp -> UB
		Thread::Sleep(100);
		const uint32_t ID = threadID;
		Thread& currentThread = m_threads[ID];
		currentThread.SetPriority(ThreadPriority::Low);
		//currentThread.SetAffinity(1ull << threadID);
		{
			char name[32];
			snprintf(name, sizeof(name), "Worker{%u}", ID);
			currentThread.SetName(name);
		}

		while (m_running.load(std::memory_order_acquire))
		{
			int w;
			m_queue.wait_dequeue(w);

			if (w == kStop)
				break;

			// process job
		}
	}
}

