#pragma once
#include <cstdint>
#include <atomic>
#include <blockingconcurrentqueue.h>
#include <Engine/Thread/Thread.h>

namespace Zero
{
	class BlockingThreadPool
	{
	public:
		// Not NUMA aware
		void Init(uint32_t threadCount = 1);

		void Shutdown();
		static BlockingThreadPool& Get();
	private:
		void worker_loop(uint32_t threadID);

		BlockingThreadPool();
		Thread* m_threads = nullptr;
		size_t m_count = 0;
		std::atomic<bool> m_running{ false };

		moodycamel::BlockingConcurrentQueue<int> m_queue;
	};
}
