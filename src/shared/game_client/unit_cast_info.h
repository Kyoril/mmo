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
		/// name display only; this struct itself never dereferences it. Consumers
		/// (the nameplate cast bar) do read name() from it, which is safe because
		/// the client's spell data lives for the whole session.
		const proto_client::SpellEntry* spell = nullptr;

		/// Id of the spell being cast, or 0 while idle. Used to disambiguate
		/// SpellGo/SpellFailure broadcasts for unrelated casts by the same unit.
		uint32 spellId = 0;

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
		void BeginCast(const proto_client::SpellEntry& castSpell, const uint32 castSpellId, const GameTime now, const GameTime castTimeMs)
		{
			spell = &castSpell;
			spellId = castSpellId;
			startTime = now;
			endTime = now + castTimeMs;
			channeling = false;
			interruptedAt = 0;
		}

		/// Starts tracking a channeled cast.
		void BeginChannel(const proto_client::SpellEntry& castSpell, const uint32 castSpellId, const GameTime now, const GameTime durationMs)
		{
			BeginCast(castSpell, castSpellId, now, durationMs);
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
				Clear();
				return;
			}

			endTime = now + timeLeftMs;
		}

		/// Clears a successfully completed cast (SpellGo) without any failure
		/// feedback. No-op unless the given spell id matches the tracked, non-
		/// channeling cast: SpellGo is also broadcast at the start of a channel
		/// (the channel is ended later via UpdateChannel(0)) and for unrelated
		/// instant/proc casts by the same unit, neither of which should clear a
		/// tracked cast bar.
		void FinishSucceeded(const uint32 endedSpellId)
		{
			if (!IsActive() || channeling || endedSpellId != spellId)
			{
				return;
			}

			Clear();
		}

		/// Clears the cast and stamps the interrupt flash - but only if the failure
		/// is for the tracked cast (failures of instant casts, or of unrelated
		/// casts by the same unit, never touch the bar).
		void FinishFailed(const GameTime now, const uint32 failedSpellId)
		{
			if (!IsActive() || failedSpellId != spellId)
			{
				return;
			}

			Clear();
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
			if (now < startTime)
			{
				// Before the tracked start (e.g. a stale/out-of-order update): report
				// the bar's starting value rather than relying on the clamp below to
				// paper over the unsigned time subtraction wrapping around.
				return channeling ? 1.0f : 0.0f;
			}

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

	private:
		/// Unconditionally clears the tracked spell/id/times/channeling flag.
		/// Does NOT touch interruptedAt - callers that want the interrupt flash
		/// stamp it themselves after calling this.
		void Clear()
		{
			spell = nullptr;
			spellId = 0;
			startTime = 0;
			endTime = 0;
			channeling = false;
		}
	};
}
