// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "request.h"
#include "response.h"

#include "base/typedefs.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>

namespace mmo
{
	namespace net
	{
		namespace https_client
		{
			/// Formats the request line and headers (up to and including the empty line).
			std::string FormatRequestHead(const Request &request);

			/// Limits applied to a single request. The defaults wait forever and cannot be cancelled.
			struct RequestOptions
			{
				/// How long one network operation (resolve, connect, handshake, a single read
				/// or write) may go without completing. This is an inactivity limit, not a limit
				/// on the whole transfer, so large downloads over slow links still succeed.
				/// Zero disables it.
				std::chrono::milliseconds inactivityTimeout{ 0 };

				/// When set and it becomes true, the request is aborted as soon as possible.
				/// It is polled, so it may be flipped from any thread.
				const std::atomic<bool>* cancel = nullptr;

				/// Called with the number of response body bytes each time some arrived, as
				/// they arrive. Lets a caller show transfer progress of large bodies, which
				/// are otherwise only handed over once complete.
				std::function<void(std::size_t bytes)> onBodyReceived;
			};

			/// Sends `request` and reads the whole response into memory.
			///
			/// Throws asio::system_error on network failure: asio::error::timed_out when the
			/// inactivity timeout elapsed and asio::error::operation_aborted when cancelled.
			https_client::Response sendRequest(
			    const std::string &host,
				uint16 port,
			    const Request &request,
				const RequestOptions &options = RequestOptions()
			);
		}
	}
}
