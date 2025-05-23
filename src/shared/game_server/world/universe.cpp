// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "universe.h"

namespace mmo
{
	Universe::Universe(asio::io_service& ioService, TimerQueue& timers)
		: m_ioService(ioService)
		, m_timers(timers)
	{
	}
}
