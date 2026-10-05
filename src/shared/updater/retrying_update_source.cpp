// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "retrying_update_source.h"
#include "update_errors.h"

#include <algorithm>
#include <thread>

namespace mmo::updating
{
	std::chrono::milliseconds GetRetryDelay(const RetryPolicy& policy, const uint32 failedAttempts)
	{
		std::chrono::milliseconds delay = policy.initialDelay;
		for (uint32 i = 1; i < failedAttempts && delay < policy.maxDelay; ++i)
		{
			delay *= 2;
		}

		return std::min(delay, policy.maxDelay);
	}

	RetryingUpdateSource::RetryingUpdateSource(
		std::unique_ptr<IUpdateSource> inner,
		RetryPolicy policy,
		const std::atomic<bool>* cancel)
		: m_inner(std::move(inner))
		, m_policy(policy)
		, m_cancel(cancel)
	{
	}

	UpdateSourceFile RetryingUpdateSource::readFile(const std::string& path)
	{
		const uint32 maxAttempts = std::max<uint32>(1, m_policy.maxAttempts);

		for (uint32 attempt = 1; ; ++attempt)
		{
			if (IsCancelled())
			{
				throw UpdateCancelled();
			}

			try
			{
				UpdateSourceFile file = m_inner->readFile(path);
				if (attempt > 1 && onRecovered)
				{
					onRecovered(path);
				}

				return file;
			}
			catch (const UpdateCancelled&)
			{
				throw;
			}
			catch (const PermanentSourceError&)
			{
				throw;
			}
			catch (const std::exception& ex)
			{
				// A cancelled transfer usually surfaces as an ordinary network error, so
				// the flag is what tells the two apart.
				if (IsCancelled())
				{
					throw UpdateCancelled();
				}

				if (attempt >= maxAttempts)
				{
					throw SourceUnavailableError(std::string(ex.what()) +
						" (gave up after " + std::to_string(maxAttempts) + " attempts)");
				}

				if (onRetry)
				{
					onRetry(path, attempt, maxAttempts, ex.what());
				}
			}

			Wait(GetRetryDelay(m_policy, attempt));
		}
	}

	bool RetryingUpdateSource::IsCancelled() const
	{
		return m_cancel && m_cancel->load();
	}

	void RetryingUpdateSource::Wait(const std::chrono::milliseconds delay) const
	{
		constexpr std::chrono::milliseconds slice{ 50 };

		const auto until = std::chrono::steady_clock::now() + delay;
		while (!IsCancelled())
		{
			const auto now = std::chrono::steady_clock::now();
			if (now >= until)
			{
				return;
			}

			std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(slice, until - now));
		}
	}
}
