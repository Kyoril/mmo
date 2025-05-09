// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "clock.h"
#include "signal.h"

#include <memory>


namespace mmo
{
	class TimerQueue;


	class Countdown
	{
	public:
		typedef signal<void()> EndSignal;
		
		EndSignal ended;

	public:
		explicit Countdown(TimerQueue& timers);
		~Countdown();

	public:
		GameTime GetEnd() const;

		void SetEnd(GameTime endTime) const;

		void Cancel() const;

		bool IsRunning() const { return m_running; }

	private:
		bool m_running;
		
		class Impl;
		std::shared_ptr<Impl> m_impl;
	};
}
