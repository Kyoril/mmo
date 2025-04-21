// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/timer_queue.h"


namespace mmo
{
	///
	class Universe final
	{
	private:

		Universe(const Universe& Other) = delete;
		Universe& operator=(const Universe& Other) = delete;

	public:

		explicit Universe(asio::io_service& ioService, TimerQueue& timers);

		TimerQueue& GetTimers() {
			return m_timers;
		}

		template<class Work>
		void Post(Work&& work)
		{
			m_ioService.post(std::forward<Work>(work));
		}

	private:

		asio::io_service& m_ioService;
		TimerQueue& m_timers;
	};
}