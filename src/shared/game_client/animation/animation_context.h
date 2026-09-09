// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/object_type_id.h"

namespace mmo
{
	/// @brief Weapon category of the currently equipped main-hand item, used to pick
	///	weapon-specific combat-idle override sets from an animation profile.
	namespace anim_weapon_class
	{
		enum Type
		{
			Unarmed,
			OneHanded,
			TwoHanded
		};
	}

	/// @brief Per-frame input snapshot for the animation controller. GameUnitC fills this
	///	from its movement, combat and replicated field state; the controller derives all
	///	animation decisions from it and never reaches back into gameplay code.
	struct AnimationContext
	{
		/// Frame time in seconds.
		float deltaTime{0.0f};

		/// Raw movement flags (movement_flags::*), used to derive the movement direction.
		uint32 movementFlags{0};

		/// True while the unit is moving on the ground with significant input.
		bool moving{false};

		/// True while the unit follows a server movement path (creatures, charge).
		bool pathMoving{false};

		/// True while the unit is airborne (jumping or falling) with significant vertical velocity.
		bool airborne{false};

		/// Vertical velocity in world units per second; positive = ascending. Only meaningful
		/// while airborne.
		float verticalVelocity{0.0f};

		/// True while the unit is swimming.
		bool swimming{false};

		/// True while walk mode is enabled (walk/run toggle).
		bool walkMode{false};

		/// True while the unit holds its weapons drawn (combat-ready stance).
		bool weaponDrawn{false};

		/// Weapon category of the equipped main-hand weapon.
		anim_weapon_class::Type weaponClass{anim_weapon_class::Unarmed};

		/// True while the unit has an active stealth aura.
		bool stealthed{false};

		/// True when the unit is dead (health or stand state).
		bool dead{false};

		/// True while the unit kneels in the loot pose: the replicated Looting unit flag is
		/// set AND the unit is standing still on the ground. The movement half is tested
		/// locally rather than waiting for the server to clear the flag, so the pose drops
		/// on the frame the player starts moving instead of a round trip later.
		bool looting{false};
	};
}
