// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace movement_ability
	{
		/// @brief Enumerates the kinds of movement a unit is capable of.
		enum Type
		{
			/// @brief The unit is not able to move at all.
			None = 0,

			/// @brief The unit is able to move over walkable ground.
			CanWalk = 1 << 0,

			/// @brief The unit is able to swim through water which is deep enough to swim in.
			///	Water that deep blocks the movement of a unit without this ability like a wall.
			CanSwim = 1 << 1,

			/// @brief The abilities of a unit whose data does not say otherwise.
			Default = CanWalk | CanSwim
		};
	}

	/// @brief Combination of movement_ability flags.
	typedef uint32 MovementAbilityFlags;

	namespace unit_extra_flags
	{
		/// @brief Flags of proto::UnitEntry::extraflags. They describe abilities of a creature
		///	which are neither unit nor npc flags.
		enum Type
		{
			/// @brief The creature is not able to swim, so water deep enough to swim in blocks
			///	its movement instead of making it swim.
			CannotSwim = 1 << 0
		};
	}

	namespace swim
	{
		/// @brief Submersion (water surface above the unit's feet) at which a unit starts to
		///	swim instead of walking on the ground below the water.
		constexpr float StartDepth = 1.5f;

		/// @brief Submersion a swimming unit keeps while it floats at the water surface. The swim
		///	animation lays the body out flat, so the torso stays in the water at this depth.
		constexpr float SurfaceMargin = 1.5f;
	}
}
