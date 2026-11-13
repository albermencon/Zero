#include "pch.h"
#include "Engine/Thread/Thread.h"
#include "BlockingThreadPool.h"
#include "JobCounterPool.h"
#include <Engine/Log.h>
#include <atomic>
#include <cstddef>
#include <Engine/Profiler/Profiler.h>

namespace Zero
{
	inline std::atomic<bool> initialized{ false };

	struct WorkerDesc
	{
		BlockingThreadPool* pool;
		uint32_t id;
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

		ZERO_CORE_TRACE("Initializing thread pool with {} threads", threadCount);

		m_threads = new Thread[threadCount];
		m_count = threadCount;
		m_running.store(true, std::memory_order_release);
		m_acceptingJobs.store(true, std::memory_order_release);

		for (uint32_t i = 0; i < threadCount; ++i)
		{
			WorkerDesc* desc = new WorkerDesc{ this, i };

			// 256KB of stack size
			m_threads[i] = Thread(256 * 1024, [desc]() 
			{
				char name[32];
				snprintf(name, sizeof(name), "Worker {%u}", desc->id);
				ZERO_PROFILE_THREAD(name);

				desc->pool->worker_loop(desc->id);
				delete desc; // Safe cleanup
			});
			
			char name[32];
			snprintf(name, sizeof(name), "Worker {%u}", i);
			m_threads[i].SetName(name);
			m_threads[i].SetAffinity(1ULL << i);
		}
	}

    void BlockingThreadPool::StopAcceptingJobs()
	{
		m_acceptingJobs.store(false, std::memory_order_release);
	}

	bool BlockingThreadPool::IsAcceptingJobs() const noexcept
	{
		return m_acceptingJobs.load(std::memory_order_acquire);
	}

    void BlockingThreadPool::Shutdown()
	{
		if (!initialized.load(std::memory_order_acquire))
			return;

		StopAcceptingJobs();
		wait_idle();

		m_running.store(false, std::memory_order_release);

		for (uint32_t i = 0; i < m_count; ++i)
			m_threads[i].Join();

		delete[] m_threads;
		m_threads = nullptr;
		m_count = 0;

		// Drain any unserviced jobs and reset in-flight counter
		Job discarded;
		while (m_queue.try_dequeue(discarded)) {}
		m_inFlightJobs.store(0, std::memory_order_release);

		initialized.store(false, std::memory_order_release);
	}

	BlockingThreadPool& BlockingThreadPool::Get()
	{
		static BlockingThreadPool instance;
		return instance;
	}

    void BlockingThreadPool::enqueue(Job job)
    {
		if (!m_running.load(std::memory_order_acquire) || !m_acceptingJobs.load(std::memory_order_acquire))
		{
			ZERO_CORE_ASSERT(false, "Cannot enqueue to inactive or sealed thread pool");
			return;
		}
		m_inFlightJobs.fetch_add(1, std::memory_order_relaxed);
		m_queue.enqueue(get_producer_token(), job);
    }

    void BlockingThreadPool::enqueue_bulk(std::span<const Job> jobs)
    {
		if (jobs.empty())
			return;

		if (!m_running.load(std::memory_order_acquire) || !m_acceptingJobs.load(std::memory_order_acquire))
		{
			ZERO_CORE_ASSERT(false, "Cannot enqueue to inactive or sealed thread pool");
			return;
		}
		m_inFlightJobs.fetch_add(static_cast<uint32_t>(jobs.size()), std::memory_order_relaxed);
		m_queue.enqueue_bulk(get_producer_token(), jobs.data(), jobs.size());
    }

    void BlockingThreadPool::enqueue(Job job, JobCounter &counter)
    {
		if (!m_running.load(std::memory_order_acquire) || !m_acceptingJobs.load(std::memory_order_acquire))
		{
			ZERO_CORE_ASSERT(false, "Cannot enqueue to inactive or sealed thread pool");
			return;
		}
        counter.pending.fetch_add(1, std::memory_order_relaxed);
		m_inFlightJobs.fetch_add(1, std::memory_order_relaxed);
		job.SetCounter(&counter);  // stamp before enqueue
		m_queue.enqueue(get_producer_token(), job);
    }

    void BlockingThreadPool::enqueue_bulk(std::span<const Job> jobs, JobCounter &counter)
    {
		if (jobs.empty())
			return;

		if (!m_running.load(std::memory_order_acquire) || !m_acceptingJobs.load(std::memory_order_acquire))
		{
			ZERO_CORE_ASSERT(false, "Cannot enqueue to inactive or sealed thread pool");
			return;
		}
        counter.pending.fetch_add(
			static_cast<uint32_t>(jobs.size()), std::memory_order_relaxed);
		m_inFlightJobs.fetch_add(
			static_cast<uint32_t>(jobs.size()), std::memory_order_relaxed);

		auto& token = get_producer_token();
		constexpr size_t BATCH_SIZE = 64;
		Job chunk[BATCH_SIZE];

		size_t remaining = jobs.size();
		size_t offset = 0;
		while (remaining > 0)
		{
			const size_t batch = std::min(remaining, BATCH_SIZE);
			for (size_t i = 0; i < batch; ++i)
			{
				chunk[i] = jobs[offset + i];
				chunk[i].SetCounter(&counter);
			}
			m_queue.enqueue_bulk(token, chunk, batch);
			offset += batch;
			remaining -= batch;
		}
    }

    JobCounter &BlockingThreadPool::make_counter()
    {
        return JobCounterPool::Get().Allocate();
    }

    void BlockingThreadPool::wait_idle()
    {
		while (m_inFlightJobs.load(std::memory_order_acquire) > 0)
        	std::this_thread::yield();
    }

    void BlockingThreadPool::worker_loop(uint32_t threadID)
	{
		moodycamel::ConsumerToken token(m_queue);
		while (m_running.load(std::memory_order_relaxed) || m_inFlightJobs.load(std::memory_order_acquire) > 0)
		{
			Job buffer[64];
			size_t count = m_queue.wait_dequeue_bulk_timed(token, buffer, 64, 250000); // 250ms
			
			for (size_t i = 0; i < count; ++i)
			{
				ZERO_PROFILE_SCOPE("Job");
				Job& job = buffer[i];

				if (job.IsInline())
					job.fn(job.payload);
				else
					job.fn(job.ptr);

				if (JobCounter* c = job.GetCounter())
            		c->Decrement(); // wake waiter if this was the last job

				m_inFlightJobs.fetch_sub(1, std::memory_order_release);
			}
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
