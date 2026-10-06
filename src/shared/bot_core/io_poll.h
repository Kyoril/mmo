// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "asio/io_service.hpp"

namespace mmo
{
	/// Polls the io object, first restarting it if it has stopped.
	/// An io object that ran out of work is left in the stopped state, and from then on poll()
	/// returns without running anything until restart() is called. A bot whose connections have
	/// all closed (e.g. displaced by a duplicate login) would otherwise never see the handlers of
	/// its reconnect.
	/// @param io The io object to drive.
	/// @return Number of handlers executed.
	inline size_t PollRestarting(asio::io_service& io)
	{
		if (io.stopped())
		{
			io.restart();
		}

		return io.poll();
	}
}
