#pragma once
#include <cstdint>
#include <atomic>
#include <blockingconcurrentqueue.h>
#include <Engine/Thread/Thread.h>
#include <Engine/JobSystem/Job.h>
#include <Engine/JobSystem/JobCounter.h>

namespace Zero
{
	class BlockingThreadPool
	{
	public:
		// Not NUMA aware
		void Init(uint32_t threadCount = 1);

		void Shutdown();
		static BlockingThreadPool& Get();

		void enqueue(Job job);
		void enqueue_bulk(std::span<const Job> jobs);

		// Returns a counter you can wait on — counter.pending is pre-incremented
        // before enqueue so there's no race between submit and wait.
        void enqueue(Job job, JobCounter& counter);

		// Bulk with counter — counter.pending += jobs.size() atomically before enqueue
        void enqueue_bulk(std::span<const Job> jobs, JobCounter& counter);

		// Allocate a counter from the internal pool.
        // Always pair with counter.release() after wait().
        [[nodiscard]] JobCounter& make_counter();

		// Block until all enqueued jobs are complete (global drain)
        void wait_idle();
		// Not implemented ends here
	
	private:
		void worker_loop(uint32_t threadID);

		moodycamel::ProducerToken& get_producer_token();

		BlockingThreadPool();
		Thread* m_threads = nullptr;
		size_t m_count = 0;
		std::atomic<bool> m_running{ false };

		moodycamel::BlockingConcurrentQueue<Job> m_queue;
	};
}
