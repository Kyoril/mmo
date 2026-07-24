// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>

namespace mmo
{
	namespace proto_client
	{
		class SpellEntry;
	}

	/// Client-side cast state of a single unit, fed by the SpellStart / SpellGo /
	/// SpellFailure / ChannelStart / ChannelUpdate packets (which arrive for every
	/// caster in view). Progress is derived from timestamps at read time, so no
	/// per-frame updates or timers are required. Consumed by the nameplate cast bar.
	struct UnitCastInfo final
	{
		/// How long the "Interrupted" flash stays visible after a failed cast.
		static constexpr GameTime InterruptFlashDurationMs = 800;

		/// The spell being cast, or nullptr while idle. Stored for identity and
		/// name display only; this struct never dereferences it.
		const proto_client::SpellEntry* spell = nullptr;

		/// Client timestamp (GetAsyncTimeMs) when the cast/channel started.
		GameTime startTime = 0;

		/// Client timestamp when the cast/channel will finish.
		GameTime endTime = 0;

		/// True while channeling (bar drains instead of filling).
		bool channeling = false;

		/// Client timestamp of the last mid-cast failure (0 = none). Drives the
		/// time-limited "Interrupted" flash; expires by comparison, never cleaned up.
		GameTime interruptedAt = 0;

		/// Starts tracking a regular cast.
		void BeginCast(const proto_client::SpellEntry& castSpell, const GameTime now, const GameTime castTimeMs)
		{
			spell = &castSpell;
			startTime = now;
			endTime = now + castTimeMs;
			channeling = false;
			interruptedAt = 0;
		}

		/// Starts tracking a channeled cast.
		void BeginChannel(const proto_client::SpellEntry& castSpell, const GameTime now, const GameTime durationMs)
		{
			BeginCast(castSpell, now, durationMs);
			channeling = true;
		}

		/// Applies a server channel update (pushback or, with timeLeftMs == 0, the
		/// regular end-of-channel signal, which clears without an interrupt flash).
		void UpdateChannel(const GameTime now, const GameTime timeLeftMs)
		{
			if (!IsActive())
			{
				return;
			}

			if (timeLeftMs == 0)
			{
				FinishSucceeded();
				return;
			}

			endTime = now + timeLeftMs;
		}

		/// Clears the cast without any failure feedback (SpellGo / channel end).
		void FinishSucceeded()
		{
			spell = nullptr;
			startTime = 0;
			endTime = 0;
			channeling = false;
		}

		/// Clears the cast and stamps the interrupt flash - but only if a cast was
		/// actually tracked (failures of instant casts never showed a bar).
		void FinishFailed(const GameTime now)
		{
			if (!IsActive())
			{
				return;
			}

			FinishSucceeded();
			interruptedAt = now;
		}

		/// Whether a cast or channel is currently tracked.
		[[nodiscard]] bool IsActive() const
		{
			return spell != nullptr;
		}

		/// Bar fill fraction at the given time: casts fill 0 -> 1, channels drain
		/// 1 -> 0. Clamped to [0, 1].
		[[nodiscard]] float GetProgress(const GameTime now) const
		{
			float progress = 1.0f;
			if (endTime > startTime)
			{
				progress = static_cast<float>(now - startTime) / static_cast<float>(endTime - startTime);
				progress = std::clamp(progress, 0.0f, 1.0f);
			}

			return channeling ? 1.0f - progress : progress;
		}

		/// Whether the "Interrupted" flash should currently be shown.
		[[nodiscard]] bool IsInterruptFlashActive(const GameTime now) const
		{
			return interruptedAt != 0 && now >= interruptedAt && now - interruptedAt < InterruptFlashDurationMs;
		}
	};
}
