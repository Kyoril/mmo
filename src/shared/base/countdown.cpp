// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "countdown.h"
#include "timer_queue.h"
#include "macros.h"


namespace mmo
{
	class Countdown::Impl
		: public std::enable_shared_from_this<Impl>
	{
	public:
		explicit Impl(TimerQueue& timers, Countdown& countdown)
			: m_timers(timers)
			, m_delayCount(0)
			, m_countdown(&countdown)
			, m_endTime(0)
		{
		}

		GameTime GetEnd() const { return m_endTime; }

		void SetEnd(const GameTime endTime)
		{
			++m_delayCount;
			m_endTime = endTime;

			if (m_countdown)
			{
				const auto delayCount = m_delayCount;

				m_countdown->m_running = true;
				m_timers.AddEvent(
					[strongThis = shared_from_this(), delayCount] { strongThis->OnPossibleEnd(delayCount); },
					endTime);
			}
		}

		void Cancel()
		{
			++m_delayCount;
			if (m_countdown)
			{
				m_countdown->m_running = false;
			}
		}

		void Kill()
		{
			ASSERT(m_countdown);
			m_countdown = nullptr;
		}

	private:

		TimerQueue& m_timers;
		size_t m_delayCount;
		Countdown* m_countdown;
		GameTime m_endTime;

	private:
		void OnPossibleEnd(size_t delayNumber) const
		{
			if (!m_countdown)
			{
				return;
			}

			if (m_delayCount != delayNumber)
			{
				return;
			}

			m_countdown->m_running = false;
			m_countdown->ended();
		}
	};
	
	Countdown::Countdown(TimerQueue& timers)
		: m_running(false)
		, m_impl(std::make_shared<Impl>(timers, *this))
	{
	}

	Countdown::~Countdown()
	{
		m_impl->Kill();
		m_running = false;
	}

	GameTime Countdown::GetEnd() const
	{
		return m_impl->GetEnd();
	}

	void Countdown::SetEnd(GameTime endTime) const
	{
		m_impl->SetEnd(endTime);
	}

	void Countdown::Cancel() const
	{
		m_impl->Cancel();
	}
}
