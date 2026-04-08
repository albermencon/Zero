#include "pch.h"
#include "BlockingThreadPool.h"
#include "JobCounterPool.h"
#include <Engine/Log.h>

namespace Zero
{
	inline std::atomic<bool> initialized{ false };

	struct WorkerDesc
	{
		BlockingThreadPool* pool;
		uint32_t id;
		Thread* thread;
	};

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
			WorkerDesc* desc = new WorkerDesc{ this, i, &m_threads[i] };
			m_threads[i] = Thread([desc]()
			{
				// desc owns itself — no race on m_threads indexing
				desc->thread->SetPriority(ThreadPriority::Low);

				char name[32];
				snprintf(name, sizeof(name), "Worker{%u}", desc->id);
				desc->thread->SetName(name);

				desc->pool->worker_loop(desc->id);
				delete desc;
			});
		}
	}

	void BlockingThreadPool::Shutdown()
	{
		if (!initialized.load(std::memory_order_acquire))
			return;

		m_running.store(false, std::memory_order_release);
		
		// poison pill
		Job poison{};
		poison.fn 		= nullptr;
		poison.counter 	= nullptr;
		poison.mode 	= Job::Mode::Inline;

		for (uint32_t i = 0; i < m_count; ++i)
			m_queue.enqueue(poison);

		for (uint32_t i = 0; i < m_count; ++i)
			m_threads[i].Join();

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

    void BlockingThreadPool::enqueue(Job job)
    {
		m_queue.try_enqueue(get_producer_token(),job);
    }

    void BlockingThreadPool::enqueue_bulk(std::span<const Job> jobs)
    {
		m_queue.try_enqueue_bulk(get_producer_token(), jobs.data(), jobs.size());
    }

    void BlockingThreadPool::enqueue(Job job, JobCounter &counter)
    {
        counter.pending.fetch_add(1, std::memory_order_relaxed);
		job.counter = &counter;  // stamp before enqueue
		m_queue.try_enqueue(get_producer_token(), job);
    }

    void BlockingThreadPool::enqueue_bulk(std::span<const Job> jobs, JobCounter &counter)
    {
        counter.pending.fetch_add(
        static_cast<uint32_t>(jobs.size()), std::memory_order_relaxed);

		std::vector<Job> stamped(jobs.begin(), jobs.end());
		for (auto& j : stamped)
			j.counter = &counter;

    	m_queue.try_enqueue_bulk(get_producer_token(), stamped.data(), stamped.size());
    }

    JobCounter &BlockingThreadPool::make_counter()
    {
        return JobCounterPool::Get().Allocate();
    }

    void BlockingThreadPool::wait_idle()
    {
		while (m_queue.size_approx() > 0)
        	std::this_thread::yield();
    }

    void BlockingThreadPool::worker_loop(uint32_t threadID)
	{
		moodycamel::ConsumerToken token(m_queue);

		while (true)
		{
			Job job;
			m_queue.wait_dequeue(token, job);
			
			// stop signal
			if (job.fn == nullptr && !m_running.load(std::memory_order_acquire))
				break;

			if (job.mode == Job::Mode::Inline)
				job.fn(job.payload);
			else
				job.fn(job.ptr);

			if (job.counter)
            	job.counter->Decrement(); // wake waiter if this was the last job
		}
	}
	
    moodycamel::ProducerToken &BlockingThreadPool::get_producer_token()
    {
        // One token per thread per queue instance.
		// Constructed once on first call from each thread.
		thread_local moodycamel::ProducerToken token(m_queue);
		return token;
    }
}
