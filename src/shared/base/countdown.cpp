
#include "countdown.h"
#include "timer_queue.h"
#include "macros.h"


namespace mmo
{
	class Countdown::Impl
		: std::enable_shared_from_this<Impl>
	{
	public:
		explicit Impl(TimerQueue& timers, Countdown& countdown)
			: m_timers(timers)
			, m_delayCount(0)
			, m_countdown(&countdown)
		{
		}

		void SetEnd(GameTime endTime)
		{
			++m_delayCount;

			m_countdown->m_running = true;
			m_timers.AddEvent(
				std::bind(&Impl::OnPossibleEnd, shared_from_this(), m_delayCount),
				endTime);
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

	void Countdown::SetEnd(GameTime endTime) const
	{
		m_impl->SetEnd(endTime);
	}

	void Countdown::Cancel() const
	{
		m_impl->Cancel();
	}
}
