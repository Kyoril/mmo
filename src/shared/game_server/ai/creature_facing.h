// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	class GameUnitS;

	/// Whether a unit is in a position to turn towards what it is acting on.
	///
	/// A creature in combat should always be facing its victim: spells routinely carry an in-front
	/// requirement, and a creature that is not facing its target simply throws the cast away. Crowd
	/// control is the exception — a stunned, slept, feared or disoriented unit does not get to
	/// choose where it is looking. Root deliberately is not an impairment here: it stops movement,
	/// not rotation.
	///
	/// @param unit The unit that would turn.
	/// @returns True when the unit may be turned to face its target.
	bool CanTurnToFaceTarget(const GameUnitS& unit);
}
