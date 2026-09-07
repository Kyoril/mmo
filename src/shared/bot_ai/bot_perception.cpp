// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_perception.h"

#include "bot_core/bot_context.h"
#include "bot_core/bot_movement_math.h"
#include "bot_core/bot_unit.h"

namespace mmo
{
	namespace
	{
		/// How far out the bot looks for something to attack. Matched to the range the server
		/// keeps units visible at; looking further would only find units the object manager has
		/// already forgotten about.
		constexpr float DefaultScanRange = 40.0f;
	}

	void BotPerception::Refresh(const BotContext& world)
	{
		*this = BotPerception{};

		if (!world.IsWorldReady())
		{
			return;
		}

		const BotUnit* self = world.GetSelf();
		if (!self)
		{
			return;
		}

		valid = true;
		selfGuid = self->GetGuid();
		// Deliberately not self->GetPosition(). The server never echoes a client its own
		// movement, so the position on the replicated unit is wherever the bot spawned; the live
		// one is the client's own simulation, which is also what the movement controller steers
		// by. Reading the stale one makes a bot that has walked to its target still believe it is
		// standing where it logged in.
		position = world.GetPosition();
		level = self->GetLevel();
		mapId = world.GetCurrentMapId();
		alive = self->IsAlive();
		healthFraction = self->GetHealthPercent();
		powerFraction = world.GetSelfPowerPercent();
		hasPower = world.GetSelfMaxPower() > 0;
		moving = world.IsMoving();
		autoAttacking = world.IsAutoAttacking();

		targetGuid = self->GetTargetGuid();
		if (targetGuid != 0)
		{
			if (const BotUnit* target = world.GetUnit(targetGuid))
			{
				targetExists = true;
				targetAlive = target->IsAlive();
				targetDistance = PlanarDistance(position, target->GetPosition());
			}
		}

		if (const BotUnit* attackable = world.GetNearestAttackable(DefaultScanRange))
		{
			nearestAttackableGuid = attackable->GetGuid();
			nearestAttackableDistance = PlanarDistance(position, attackable->GetPosition());
		}

		attackerCount = static_cast<uint32>(world.GetUnitsTargetingSelf(DefaultScanRange).size());
	}
}
