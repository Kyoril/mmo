// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "update_source.h"

#include "base/typedefs.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace mmo::updating
{
	/// How often and how patiently RetryingUpdateSource asks again.
	struct RetryPolicy
	{
		/// Total number of attempts per file, including the first one.
		uint32 maxAttempts = 6;

		/// The wait before the second attempt. It doubles for every further attempt.
		std::chrono::milliseconds initialDelay{ 1000 };

		/// Upper bound for the wait between two attempts.
		std::chrono::milliseconds maxDelay{ 16000 };
	};

	/// Returns the wait before attempt `failedAttempts + 1`, given that `failedAttempts`
	/// attempts (at least one) have failed so far.
	std::chrono::milliseconds GetRetryDelay(const RetryPolicy& policy, uint32 failedAttempts);

	/// Decorates another update source and retries reads that failed for a reason that
	/// may be temporary, like a dropped connection or a timeout.
	///
	/// Retrying here, around a single file, rather than around a whole update step means
	/// that one flaky download costs one file instead of restarting a large archive, and
	/// that the prepare step's manifest download is covered too. This relies on the
	/// wrapped source returning only complete files: a source that streams lazily would
	/// fail later, outside of readFile, where no retry can catch it.
	///
	/// Errors marked as PermanentSourceError are passed on unchanged. When the last
	/// attempt fails, SourceUnavailableError is thrown.
	///
	/// readFile may be called from several threads at once, as long as the wrapped source
	/// allows it. The callbacks are invoked on the calling thread.
	struct RetryingUpdateSource final : IUpdateSource
	{
		/// Invoked after attempt `attempt` of `maxAttempts` failed and before waiting for
		/// the next one.
		typedef std::function<void(const std::string& path, uint32 attempt, uint32 maxAttempts, const std::string& error)> RetryCallback;

		/// Invoked when a read succeeded after at least one failed attempt.
		typedef std::function<void(const std::string& path)> RecoveredCallback;

		/// `cancel` may be null. When it becomes true, waiting between attempts ends early
		/// and UpdateCancelled is thrown.
		RetryingUpdateSource(
			std::unique_ptr<IUpdateSource> inner,
			RetryPolicy policy,
			const std::atomic<bool>* cancel);

		UpdateSourceFile readFile(const std::string& path) override;

		RetryCallback onRetry;
		RecoveredCallback onRecovered;

	private:
		bool IsCancelled() const;

		/// Sleeps for `delay` in short slices so that cancellation is noticed quickly.
		void Wait(std::chrono::milliseconds delay) const;

		std::unique_ptr<IUpdateSource> m_inner;
		RetryPolicy m_policy;
		const std::atomic<bool>* m_cancel;
	};
}
