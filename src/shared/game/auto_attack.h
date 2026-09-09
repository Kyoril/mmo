// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	namespace attack_swing_event
	{
		enum Type
		{
			/// Can't auto attack while moving (used for ranged auto attacks)
			NotStanding = 0,

			/// Target is out of range (or too close in case of ranged auto attacks).
			OutOfRange = 1,

			/// Can't attack that target (invalid target).
			CantAttack = 2,

			/// Target has to be in front of us (we need to look at the target).
			WrongFacing = 3,

			/// The target is dead and thus can not be attacked.
			TargetDead = 4,

			/// Successful auto attack swing. Sent to the client when a swing lands again after a
			/// failure, so it stops repeating the error message and voice line for that failure.
			Success = 0xFFFFFFFE,

			/// No swing outcome known yet. Server-internal reset value so the next real outcome
			/// counts as a change; never sent to the client.
			Unknown = 0xFFFFFFFF
		};
	}

	typedef attack_swing_event::Type AttackSwingEvent;
}