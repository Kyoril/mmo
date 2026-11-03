// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pet_behavior.h"

#include "math/math_utils.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		/// Minimum distance between two neighbouring slots on the arc, with a little slack.
		constexpr float swarmMinChord = 1.05f;

		Vector3 SlotAt(const Vector3& ownerPos, const Radian& ownerFacing, const float angleOffset, const float distance)
		{
			return ownerPos + FacingToDirection(Radian(ownerFacing.GetValueRadians() + angleOffset)) * distance;
		}
	}

	Vector3 PetFollowSlot(const Vector3& ownerPos, const Radian& ownerFacing, const uint32 slotIndex, const uint32 slotCount)
	{
		// A facing turns counter-clockwise seen from above: the right is a quarter turn back.
		if (slotIndex == 0)
		{
			return SlotAt(ownerPos, ownerFacing, -Pi * 0.5f, PetFollowSlotDistance);
		}

		if (slotIndex == 1)
		{
			return SlotAt(ownerPos, ownerFacing, Pi * 0.5f, PetFollowSlotDistance);
		}

		if (slotCount <= 3)
		{
			return SlotAt(ownerPos, ownerFacing, Pi, PetFollowSlotDistance);
		}

		// A swarm: everything after the right and left slot spreads on an arc behind the owner. Large
		// swarms squeeze the spacing to stay behind the owner and push the arc out instead.
		// Neighbours on the arc are 30 degrees apart, and the arc covers at most 65 degrees to each
		// side of straight behind, which keeps it clear of the right and left slot.
		const float swarmSpacing = Pi / 6.0f;
		const float swarmHalfSpan = Pi * 65.0f / 180.0f;
		const uint32 arcCount = slotCount - 2;
		const uint32 arcIndex = std::min(slotIndex, slotCount - 1) - 2;
		const float spacing = std::min(swarmSpacing, 2.0f * swarmHalfSpan / static_cast<float>(arcCount - 1));
		const float radius = std::max(PetSwarmArcRadius, swarmMinChord / (2.0f * std::sin(spacing * 0.5f)));
		const float offset = Pi + (static_cast<float>(arcIndex) - static_cast<float>(arcCount - 1) * 0.5f) * spacing;
		return SlotAt(ownerPos, ownerFacing, offset, radius);
	}

	uint64 ChoosePetTarget(const uint64 commandedTarget, const uint64 ownerVictim, const std::vector<uint64>& ownerAttackers, const std::vector<uint64>& selfAttackers, const pet_strategy::Type strategy)
	{
		if (commandedTarget != 0)
		{
			return commandedTarget;
		}

		if (strategy == pet_strategy::Passive)
		{
			return 0;
		}

		if (ownerVictim != 0)
		{
			return ownerVictim;
		}

		if (!ownerAttackers.empty())
		{
			return ownerAttackers.front();
		}

		if (!selfAttackers.empty())
		{
			return selfAttackers.front();
		}

		return 0;
	}

	float PetRunSpeed(const float ownSpeed, const float ownerSpeed)
	{
		return std::max(ownSpeed, ownerSpeed);
	}
}
