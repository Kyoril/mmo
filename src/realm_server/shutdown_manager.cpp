// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shutdown_manager.h"

#include "base/clock.h"
#include "base/macros.h"
#include "game/shutdown_countdown.h"

namespace mmo
{
	ShutdownManager::ShutdownManager(Clock clock, Scheduler scheduler)
		: m_clock(std::move(clock))
		, m_scheduler(std::move(scheduler))
	{
	}

	void ShutdownManager::Schedule(const uint32 delaySeconds)
	{
		ASSERT(delaySeconds <= MaxShutdownDelaySeconds);

		++m_generation;
		m_pending = true;
		m_deadline = m_clock() + static_cast<GameTime>(delaySeconds) * constants::OneSecond;

		if (delaySeconds > 0)
		{
			announce(delaySeconds);
		}

		ArmNext(delaySeconds);
	}

	bool ShutdownManager::Cancel()
	{
		if (!m_pending)
		{
			return false;
		}

		++m_generation;
		m_pending = false;
		announce(ShutdownCountdownCancelled);
		return true;
	}

	uint32 ShutdownManager::GetRemainingSeconds() const
	{
		if (!m_pending)
		{
			return 0;
		}

		const GameTime now = m_clock();
		if (now >= m_deadline)
		{
			return 0;
		}

		return static_cast<uint32>((m_deadline - now + constants::OneSecond - 1) / constants::OneSecond);
	}

	void ShutdownManager::ArmNext(const uint32 remainingSeconds)
	{
		const uint32 mark = NextShutdownAnnouncement(remainingSeconds);
		const uint32 generation = m_generation;
		m_scheduler([this, generation, mark]() { OnTimer(generation, mark); },
			m_deadline - static_cast<GameTime>(mark) * constants::OneSecond);
	}

	void ShutdownManager::OnTimer(const uint32 generation, const uint32 mark)
	{
		if (generation != m_generation || !m_pending)
		{
			return;
		}

		if (mark == 0)
		{
			++m_generation;
			m_pending = false;
			shutdownDue();
			return;
		}

		announce(mark);
		ArmNext(mark);
	}
}
