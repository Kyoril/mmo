// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/pet_defines.h"
#include "math/radian.h"
#include "math/vector3.h"

#include <vector>

namespace mmo
{
	/// Interval of the pet AI's follow / engage poll while it is not fighting (milliseconds).
	constexpr GameTime PetUpdateInterval = 500;

	/// Distance of the first three follow slots (right, left, behind) to the owner.
	constexpr float PetFollowSlotDistance = 2.0f;

	/// Radius of the arc behind the owner on which the pets of a swarm spread out.
	constexpr float PetSwarmArcRadius = 2.5f;

	/// A pet further away from its owner than this teleports to its owner's side.
	constexpr float PetTeleportDistance = 100.0f;

	/// A pet that has no path to its follow slot teleports there when it is further away than this.
	constexpr float PetNoPathTeleportDistance = 5.0f;

	/// A fighting pet further away from its owner than this breaks off the fight and returns.
	constexpr float PetLeashDistance = 60.0f;

	/// Calculates where a pet stands while it follows its owner. Slot 0 is on the owner's right at
	/// PetFollowSlotDistance, slot 1 on its left, slot 2 behind it. Owners with more than three pets
	/// (swarms) keep the right and left slot and spread the others on an arc behind the owner
	/// (PetSwarmArcRadius, wider for very large swarms), so no two slots are closer than 1 yd.
	/// @param ownerPos The owner's position.
	/// @param ownerFacing The owner's facing.
	/// @param slotIndex Zero-based index of the pet among its owner's pets.
	/// @param slotCount Number of pets of the owner.
	/// @return The world position of the slot (same height as the owner).
	Vector3 PetFollowSlot(const Vector3& ownerPos, const Radian& ownerFacing, uint32 slotIndex, uint32 slotCount);

	/// Chooses what a pet should attack.
	/// Assist: the commanded target, then the owner's victim, then the first unit attacking the
	/// owner, then the first unit attacking the pet. Passive: the commanded target only.
	/// @param commandedTarget Guid of the target the owner commanded the pet to attack, or 0.
	/// @param ownerVictim Guid of the unit the owner attacks, or 0.
	/// @param ownerAttackers Guids of units attacking the owner.
	/// @param selfAttackers Guids of units attacking the pet.
	/// @param strategy The pet's strategy.
	/// @return Guid of the target, or 0 if the pet should not fight.
	uint64 ChoosePetTarget(uint64 commandedTarget, uint64 ownerVictim, const std::vector<uint64>& ownerAttackers, const std::vector<uint64>& selfAttackers, pet_strategy::Type strategy);

	/// @return The run speed of a pet: its own, or its owner's if that is faster, so it keeps up.
	float PetRunSpeed(float ownSpeed, float ownerSpeed);
}
