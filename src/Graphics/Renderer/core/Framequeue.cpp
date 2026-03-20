#include "pch.h"
#include <Engine/Core.h>
#include <Engine/Log.h>
#include "Framequeue.h"

namespace Zero
{

	FrameQueue::FrameQueue(size_t frames_inflight)
		: m_queue(frames_inflight), m_availableSlots(frames_inflight)
	{
	}

	void FrameQueue::Push(std::unique_ptr<FrameData> frame)
	{
		//m_availableSlots.acquire(); // blocks if no slots remaining
		//m_queue.wait_enqueue(std::move(frame));

		bool ok = m_queue.try_enqueue(std::move(frame));
		ENGINE_CORE_ASSERT(ok, "Invariant broken: queue must have space after acquire");
	}

	bool FrameQueue::TryPush(std::unique_ptr<FrameData> frame)
	{
		if (!m_availableSlots.try_acquire())
			return false;
		return m_queue.try_enqueue(std::move(frame));
	}

	bool FrameQueue::Pop(std::unique_ptr<FrameData>& out)
	{
		return m_queue.try_dequeue(out);
	}

	void FrameQueue::FrameConsumed()
	{
		m_availableSlots.release();
	}

	bool FrameQueue::TryAdquire()
	{
		return m_availableSlots.try_acquire();
	}
}
