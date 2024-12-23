// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "timer_queue.h"
#include "macros.h"
#include "clock.h"


namespace mmo
{
	TimerQueue::TimerQueue(asio::io_service &service)
		: m_timer(service)
	{
	}

	GameTime TimerQueue::GetNow() const
	{
		return GetAsyncTimeMs();
	}

	void TimerQueue::AddEvent(const EventCallback& callback, GameTime time)
	{
		m_queue.emplace(callback, time);
		SetTimer();
	}

	void TimerQueue::Update(const asio::system_error &error)
	{
		if (error.code())
		{
			// There was an error, which most likely means that the timer is no longer valid / has been stopped,
			// so we will simply ignore the timers callback from here on.
			return;
		}

		m_timerTime.reset();

		const auto now = GetNow();
		while (!m_queue.empty())
		{
			const auto &next = m_queue.top();

			if (now >= next.time)
			{
				const auto callback = next.callback;
				m_queue.pop();
				callback();
			}
			else
			{
				SetTimer();
				break;
			}
		}
	}

	void TimerQueue::SetTimer()
	{
		const auto nextEventTime = m_queue.top().time;

		// Is the timer active?
		if (m_timerTime)
		{
			if (nextEventTime < *m_timerTime)
			{
				m_timer.cancel();
			}
			else
			{
				return;
			}
		}

		const auto now = GetNow();
		m_timerTime = nextEventTime;

		const auto delay = (std::max(nextEventTime, now) - now);
		m_timer.expires_from_now(std::chrono::milliseconds(delay));

		m_timer.async_wait(std::bind(&TimerQueue::Update, this, std::placeholders::_1));
	}
}
