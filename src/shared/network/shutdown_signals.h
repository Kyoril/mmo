// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "asio/io_service.hpp"
#include "asio/signal_set.hpp"

#include <csignal>
#include <functional>
#include <memory>
#include <utility>

namespace mmo
{
	/// Installs a handler for the signals that mean "shut down cleanly".
	///
	/// The returned signal_set must be kept alive for as long as the handler should stay armed.
	/// It is also the handle the shutdown path needs: a pending async_wait counts as outstanding
	/// io_service work, so a shutdown that waits for the service to drain would otherwise wait
	/// forever. Cancel it as part of shutting down.
	///
	/// SIGINT and SIGTERM are registered on every platform. On Windows SIGBREAK is added as well,
	/// because CTRL_BREAK_EVENT is the only way one process can ask another to stop cleanly there
	/// -- Windows never raises SIGTERM itself, and TerminateProcess (what PowerShell's
	/// Stop-Process -Force does) gives the target no notification at all. In Docker, which is the
	/// deployment that matters for these servers, SIGTERM is the signal that arrives.
	inline std::unique_ptr<asio::signal_set> InstallShutdownHandler(
		asio::io_service& ioService, std::function<void()> handler)
	{
		auto signals = std::make_unique<asio::signal_set>(ioService, SIGINT, SIGTERM);

#ifdef SIGBREAK
		signals->add(SIGBREAK);
#endif

		signals->async_wait([handler = std::move(handler)](const asio::error_code& error, int)
		{
			// operation_aborted means the wait was cancelled as part of shutting down, which is
			// not itself a request to shut down again.
			if (!error)
			{
				handler();
			}
		});

		return signals;
	}
}
