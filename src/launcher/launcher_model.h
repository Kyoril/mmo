// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"

#include <atomic>
#include <mutex>
#include <string>

namespace mmo
{
	enum class UpdatePhase
	{
		Connecting,
		Preparing,
		Updating,
		Ready,
		Failed
	};

	/// A consistent picture of the update state, copied out for the UI thread to draw.
	struct UpdateSnapshot
	{
		UpdatePhase phase = UpdatePhase::Connecting;
		std::string statusText;
		/// 0..1. Negative means indeterminate: the total is not known yet.
		float progress = -1.0f;
		bool playEnabled = false;
	};

	/// The single point of contact between the update worker and the UI.
	///
	/// The worker writes; the UI thread polls. Polling rather than posting a message
	/// per progress callback is deliberate: updateFile is called per chunk per file
	/// across several worker threads and would flood the message queue. Instead every
	/// mutation bumps a version counter, and the UI thread does one relaxed atomic load
	/// per frame, taking the lock only when something actually changed.
	class LauncherModel final : public NonCopyable
	{
	public:
		void SetPhase(UpdatePhase phase);
		void SetStatus(std::string text);

		/// `progress` is 0..1, or negative for indeterminate.
		void SetProgress(float progress);

		/// Moves to Ready and enables Play.
		void SetReady(std::string text);

		/// Moves to Failed and reports `error` as the status.
		void SetFailed(std::string error);

		/// Fills `out` and returns true only if the state changed since `lastVersion`,
		/// which is updated in place. UI thread only.
		bool TryGetSnapshot(UpdateSnapshot& out, uint32& lastVersion) const;

	private:
		template <typename F>
		void Mutate(F&& fn)
		{
			{
				const std::scoped_lock lock{ m_mutex };
				fn(m_state);
			}

			// Release so that a reader which observes this version also observes every
			// write made above it.
			m_version.fetch_add(1, std::memory_order_release);
		}

		mutable std::mutex m_mutex;
		UpdateSnapshot m_state;
		std::atomic<uint32> m_version{ 0 };
	};
}
