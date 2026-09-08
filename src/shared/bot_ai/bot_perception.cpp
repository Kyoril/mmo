// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_perception.h"

#include "bot_core/bot_context.h"
#include "bot_ai/grind_spot_index.h"
#include "bot_core/bot_movement_math.h"
#include "bot_core/bot_nav_service.h"

#include "proto_data/project.h"
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

	void BotPerception::Refresh(const BotContext& world, const BotGrindState& grind, const GameTime nowMs)
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

		// BotUnit::IsAttackableBy only rejects units carrying an NPC flag, which says nothing about
		// a town guard: guards have no flags and are perfectly attackable by that rule. The server
		// refuses the attack, so a bot that picks one stands there swinging at something that will
		// never fight back. Applying the server's own faction rule here keeps the two in agreement.
		const proto::Project* project = world.GetNavService() ? world.GetNavService()->GetProject() : nullptr;
		const uint32 selfFactionTemplate = self->GetFactionTemplate();

		const BotUnit* attackable = world.GetNearestAttackableExcept(
			[&grind, nowMs, project, selfFactionTemplate](const BotUnit& unit)
			{
				if (grind.IsUnreachable(unit.GetGuid(), nowMs))
				{
					return true;
				}

				return project != nullptr
					&& IsFactionFriendly(*project, selfFactionTemplate, unit.GetFactionTemplate());
			},
			DefaultScanRange);

		if (attackable)
		{
			nearestAttackableGuid = attackable->GetGuid();
			nearestAttackableDistance = PlanarDistance(position, attackable->GetPosition());
		}

		attackerCount = static_cast<uint32>(world.GetUnitsTargetingSelf(DefaultScanRange).size());
	}
}
