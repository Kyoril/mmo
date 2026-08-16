// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "database.h"

#include "auth_protocol/auth_connection.h"

#include <functional>
#include <memory>
#include <utility>

namespace mmo
{
	/// Returns a view of `database` whose result callbacks run on `connection`'s strand.
	///
	/// The login server runs two io threads and hands database results to a bare ioService.post(),
	/// so a completion runs on whichever thread happens to be free -- not necessarily the one that
	/// owns the session that asked for it. Every session-scoped request must therefore come back to
	/// its own strand before it touches session state, and above all before it sends anything:
	/// sendSinglePacket builds the packet directly into the connection's send buffer and patches
	/// the packet's size field in afterwards, while the connection's send completion runs on the
	/// strand and swaps that same buffer away. Run those two at once and the second destroys the
	/// packet the first is still writing -- which surfaced as the assert in StringSink::Overwrite
	/// that aborted the login server partway through a realm list.
	///
	/// Bound once per session rather than wrapped around each handler at the call site, because
	/// the call-site form only holds for as long as everyone remembers it: the realm list was sent
	/// from a completion added long after the two-thread rule was written down.
	inline AsyncDatabase MakeStrandBoundDatabase(const AsyncDatabase& database,
		std::shared_ptr<AbstractConnection<auth::Protocol>> connection)
	{
		return AsyncDatabase{
			database.GetAsyncWorker(),
			[connection = std::move(connection)](std::function<void()> action)
			{
				// Post holds the connection alive until the result runs, so a session torn down
				// while its request was still in flight resolves against a live strand rather
				// than a destroyed one. The handlers themselves hold weak references to the
				// session, so one that is gone simply does nothing.
				connection->Post(std::move(action));
			} };
	}
}
