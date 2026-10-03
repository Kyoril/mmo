// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "game_client/spell_visual_rules.h"

using namespace mmo;

TEST_CASE("Only StartCast and Casting spawn cast-phase effects", "[spell_visual]")
{
	using namespace spell_visual::event;

	CHECK(spell_visual::IsCastPhaseEvent(StartCast));
	CHECK(spell_visual::IsCastPhaseEvent(Casting));

	// Everything else must survive the terminating events: a self-heal's impact and a
	// recast buff's idle loop live on the caster, and CastSucceeded used to destroy both.
	CHECK_FALSE(spell_visual::IsCastPhaseEvent(CancelCast));
	CHECK_FALSE(spell_visual::IsCastPhaseEvent(CastSucceeded));
	CHECK_FALSE(spell_visual::IsCastPhaseEvent(Impact));
	CHECK_FALSE(spell_visual::IsCastPhaseEvent(AuraApplied));
	CHECK_FALSE(spell_visual::IsCastPhaseEvent(AuraRemoved));
	CHECK_FALSE(spell_visual::IsCastPhaseEvent(AuraTick));
	CHECK_FALSE(spell_visual::IsCastPhaseEvent(AuraIdle));
}

TEST_CASE("Timed tint keys never collide with persistent or synthetic keys", "[spell_visual]")
{
	constexpr uint32 spellId = 58;
	const uint32 timedKey = spell_visual::TimedTintKey(spellId);

	CHECK(timedKey != spellId);
	CHECK((timedKey & ~spell_visual::TimedTintKeyBit) == spellId);

	// Visualizations played by id use the 0x80000000 bit; their pulses keep both bits.
	constexpr uint32 syntheticKey = 0x80000000u | 40u;
	const uint32 syntheticTimedKey = spell_visual::TimedTintKey(syntheticKey);
	CHECK(syntheticTimedKey != syntheticKey);
	CHECK((syntheticTimedKey & 0x80000000u) != 0);
}

TEST_CASE("Tint pulse envelope ramps in, releases to zero and is zero outside its life", "[spell_visual]")
{
	constexpr float duration = 1.0f;

	CHECK(spell_visual::TintPulseEnvelope(-0.1f, duration) == 0.0f);
	CHECK(spell_visual::TintPulseEnvelope(0.0f, duration) == 0.0f);
	CHECK(spell_visual::TintPulseEnvelope(0.075f, duration) == Approx(0.5f));
	CHECK(spell_visual::TintPulseEnvelope(0.15f, duration) == Approx(1.0f));
	CHECK(spell_visual::TintPulseEnvelope(0.575f, duration) == Approx(0.5f));
	CHECK(spell_visual::TintPulseEnvelope(0.999f, duration) == Approx(0.0f).margin(0.01f));
	CHECK(spell_visual::TintPulseEnvelope(duration, duration) == 0.0f);
	CHECK(spell_visual::TintPulseEnvelope(2.0f, duration) == 0.0f);
}

TEST_CASE("Tint pulse envelope tolerates degenerate parameters", "[spell_visual]")
{
	CHECK(spell_visual::TintPulseEnvelope(0.1f, 0.0f) == 0.0f);
	CHECK(spell_visual::TintPulseEnvelope(0.1f, -1.0f) == 0.0f);

	// No attack: starts at full strength.
	CHECK(spell_visual::TintPulseEnvelope(0.0f, 1.0f, 0.0f) == Approx(1.0f));
}
