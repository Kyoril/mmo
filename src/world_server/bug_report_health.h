// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Circuit breaker for the bug report upload path.
	///
	/// Trips after a number of consecutive failed upload attempts, or when the upload queue is
	/// full. A tripped breaker makes the subsystem unavailable, which stops new reports at the
	/// realm, so nothing would ever be uploaded again to prove the backend is back. The breaker
	/// therefore closes on its own after a cooldown ("half-open"); the next failures trip it again.
	/// Any successful delivery closes it immediately.
	///
	/// Pure state machine, driven with explicit timestamps so it can be tested without a clock.
	class BugReportHealth final
	{
	public:
		/// @param failureThreshold Consecutive failed attempts that trip the breaker.
		/// @param cooldownMs How long a tripped breaker stays open before it closes again.
		explicit BugReportHealth(const uint32 failureThreshold = 3, const GameTime cooldownMs = 60 * 1000)
			: m_failureThreshold(failureThreshold)
			, m_cooldownMs(cooldownMs)
		{
		}

		/// A single upload attempt failed (network error or server error).
		void OnAttemptFailed(const GameTime now)
		{
			++m_consecutiveFailures;
			if (m_consecutiveFailures >= m_failureThreshold)
			{
				Trip(now);
			}
		}

		/// A report was delivered.
		void OnDelivered()
		{
			m_consecutiveFailures = 0;
			m_healthy = true;
		}

		/// The upload queue overflowed.
		void OnQueueFull(const GameTime now)
		{
			Trip(now);
		}

		/// Closes the breaker once the cooldown has elapsed.
		void Update(const GameTime now)
		{
			if (!m_healthy && now >= m_trippedAt + m_cooldownMs)
			{
				m_healthy = true;
				m_consecutiveFailures = 0;
			}
		}

		[[nodiscard]] bool IsHealthy() const
		{
			return m_healthy;
		}

	private:
		void Trip(const GameTime now)
		{
			m_healthy = false;
			m_trippedAt = now;
		}

	private:
		uint32 m_failureThreshold;
		GameTime m_cooldownMs;
		uint32 m_consecutiveFailures = 0;
		bool m_healthy = true;
		GameTime m_trippedAt = 0;
	};
}
