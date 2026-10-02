// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>

namespace mmo::spell_visual
{
	/// \brief Proto values of the spell visualization events (mirror of proto_client::SpellVisualEvent).
	///
	/// Kept as plain constants so the lifetime rules below stay testable without the client
	/// protobuf library, which a headless build does not have.
	namespace event
	{
		constexpr uint32 StartCast = 0;
		constexpr uint32 CancelCast = 1;
		constexpr uint32 Casting = 2;
		constexpr uint32 CastSucceeded = 3;
		constexpr uint32 Impact = 4;
		constexpr uint32 AuraApplied = 5;
		constexpr uint32 AuraRemoved = 6;
		constexpr uint32 AuraTick = 7;
		constexpr uint32 AuraIdle = 8;
	}

	/// \brief Whether effects spawned by an event belong to the cast phase of a spell.
	///
	/// Cast-phase effects (hand glows, channel loops, cast tints) are torn down by the
	/// terminating events CastSucceeded and CancelCast. Everything else -- impacts, aura
	/// visuals -- must survive those events: a cleric healing herself receives Impact on her
	/// own unit, and recasting a buff she already carries raises CastSucceeded on the unit
	/// that holds the aura's idle loop. Tearing down by (actor, spell) alone destroyed both.
	/// \param protoEvent Event value, see spell_visual::event.
	inline bool IsCastPhaseEvent(const uint32 protoEvent)
	{
		return protoEvent == event::StartCast || protoEvent == event::Casting;
	}

	/// \brief Bit marking a tint key as a timed pulse (ColorTint.duration_ms).
	///
	/// Timed pulses are tracked under their own key so a pulse expiring never removes a
	/// persistent tint of the same spell, and a persistent tint being removed never cuts a
	/// running pulse short. Distinct from the 0x80000000 synthetic visualization bit.
	constexpr uint32 TimedTintKeyBit = 0x40000000u;

	/// \brief Tint key a timed pulse of the given spell (or synthetic visualization key) uses.
	inline uint32 TimedTintKey(const uint32 spellId)
	{
		return spellId | TimedTintKeyBit;
	}

	/// \brief Strength multiplier of a timed tint pulse at a point in its life.
	///
	/// A short linear attack (so the glow does not pop on) followed by a linear release back to
	/// zero. Returns 0 outside [0, duration].
	/// \param elapsedSeconds Time since the pulse started.
	/// \param durationSeconds Total pulse length.
	/// \param attackFraction Fraction of the pulse spent ramping up.
	inline float TintPulseEnvelope(const float elapsedSeconds, const float durationSeconds, const float attackFraction = 0.15f)
	{
		if (durationSeconds <= 0.0f || elapsedSeconds < 0.0f || elapsedSeconds >= durationSeconds)
		{
			return 0.0f;
		}

		const float t = elapsedSeconds / durationSeconds;
		const float attack = std::clamp(attackFraction, 0.0f, 0.95f);
		if (attack > 0.0f && t < attack)
		{
			return t / attack;
		}

		return std::max(0.0f, (1.0f - t) / (1.0f - attack));
	}
}
