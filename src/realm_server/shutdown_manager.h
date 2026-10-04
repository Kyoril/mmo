// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"

#include <functional>

namespace mmo
{
	/// Counts down a scheduled realm shutdown and announces it at the marks of
	/// NextShutdownAnnouncement.
	///
	/// Time and timers are injected so the countdown can be tested without an io_service: the realm
	/// passes its TimerQueue. Queued timer events cannot be cancelled there, so every schedule and
	/// cancel bumps a generation counter and events from an older generation do nothing.
	///
	/// **Threading:** single-threaded, like the rest of the realm server.
	class ShutdownManager final : public NonCopyable
	{
	public:
		/// Returns the current time in milliseconds.
		typedef std::function<GameTime()> Clock;

		/// Runs a callback once the given time (milliseconds, same base as Clock) is reached.
		typedef std::function<void(std::function<void()>, GameTime)> Scheduler;

	public:
		/// Creates an idle shutdown manager.
		ShutdownManager(Clock clock, Scheduler scheduler);

	public:
		/// Schedules the shutdown, replacing a pending one. Announces the exact remaining time
		/// right away (except for a delay of 0, which only fires shutdownDue on the next timer run).
		/// @param delaySeconds Seconds until the shutdown; must not exceed MaxShutdownDelaySeconds.
		void Schedule(uint32 delaySeconds);

		/// Cancels the pending shutdown and announces the cancellation.
		/// @returns false if no shutdown was pending.
		bool Cancel();

		/// Whether a shutdown is currently scheduled.
		[[nodiscard]] bool IsPending() const { return m_pending; }

		/// Seconds until the pending shutdown, rounded up; 0 if none is pending.
		[[nodiscard]] uint32 GetRemainingSeconds() const;

	public:
		/// Fired with the remaining seconds at every announcement mark, or with
		/// ShutdownCountdownCancelled when a pending shutdown is cancelled.
		signal<void(uint32)> announce;

		/// Fired once when a pending shutdown reaches zero.
		signal<void()> shutdownDue;

	private:
		/// Queues the timer event for the next mark below remainingSeconds.
		void ArmNext(uint32 remainingSeconds);

		/// Handles a timer event of the given generation for the given mark (0 = due).
		void OnTimer(uint32 generation, uint32 mark);

	private:
		Clock m_clock;
		Scheduler m_scheduler;
		bool m_pending = false;
		GameTime m_deadline = 0;
		uint32 m_generation = 0;
	};
}
