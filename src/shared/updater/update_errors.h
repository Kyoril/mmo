// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <stdexcept>
#include <string>

namespace mmo::updating
{
	/// Thrown when an update was aborted on request, e.g. because the user closed the
	/// launcher. It is not a failure and should not be reported as one.
	struct UpdateCancelled : std::runtime_error
	{
		UpdateCancelled()
			: std::runtime_error("The update was cancelled")
		{
		}
	};

	/// Thrown by an update source for an error that asking again cannot fix, such as a
	/// file missing on the server. RetryingUpdateSource gives up on these immediately.
	struct PermanentSourceError : std::runtime_error
	{
		explicit PermanentSourceError(const std::string& message)
			: std::runtime_error(message)
		{
		}
	};

	/// Thrown by RetryingUpdateSource when a file could still not be read after the last
	/// attempt. The update server is unreachable or the connection is unhealthy.
	struct SourceUnavailableError : std::runtime_error
	{
		explicit SourceUnavailableError(const std::string& message)
			: std::runtime_error(message)
		{
		}
	};

	/// Returns true if an HTTP status code describes a condition that may go away on its
	/// own (server overloaded, gateway trouble, rate limiting, request timeout).
	inline bool IsTransientHttpStatus(const unsigned status)
	{
		return status == 408 || status == 429 || status >= 500;
	}
}
