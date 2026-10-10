// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_ai_reset_state.h"

#include "game_server/objects/game_creature_s.h"
#include "game_server/world/universe.h"

namespace mmo
{
	CreatureAIResetState::CreatureAIResetState(CreatureAI& ai)
		: CreatureAIState(ai)
	{
	}

	CreatureAIResetState::~CreatureAIResetState()
	= default;

	void CreatureAIResetState::OnEnter()
	{
		CreatureAIState::OnEnter();

		auto& controlled = GetControlled();
		controlled.SetMovementMode(unit_movement_mode::Run);

		controlled.RaiseTrigger(trigger_event::OnReset);
		controlled.RemoveLootRecipients();
		controlled.SetTarget(0);

		// Enter idle mode when home point was reached
		m_onHomeReached = controlled.GetMover().targetReached.connect([this]() {
			auto& ai = GetAI();
			auto* world = ai.GetControlled().GetWorldInstance();
			if (world)
			{
				auto& universe = world->GetUniverse();
				universe.Post([&ai]() {
					ai.Idle();
					});
			}
			});

		// Return to where the creature was on its patrol route when combat started rather than
		// all the way back to the spawn point.
		const bool toPatrol = GetAI().HasSavedPatrolReturnPosition();
		const Vector3 destination = toPatrol ? GetAI().GetSavedPatrolReturnPosition() : GetAI().GetHome().position;
		const Radian facing = toPatrol ? controlled.GetFacing() : Radian(GetAI().GetHome().orientation);

		auto& mover = controlled.GetMover();
		const bool walking = mover.MoveTo(destination, 0.0f, toPatrol ? nullptr : &facing);

		// No path home, or one that ends elsewhere (the creature was drawn onto another part of
		// the navigation mesh, or off it): waiting for an arrival that never comes would leave it
		// evading forever. Put it home instead.
		const float dx = mover.GetTarget().x - destination.x;
		const float dz = mover.GetTarget().z - destination.z;
		if (!walking || dx * dx + dz * dz > HomeArrivalTolerance * HomeArrivalTolerance)
		{
			WLOG("Creature 0x" << std::hex << controlled.GetGuid() << std::dec << " cannot walk home, teleporting it there");
			mover.Teleport(destination, facing);

			auto& ai = GetAI();
			if (auto* world = controlled.GetWorldInstance())
			{
				world->GetUniverse().Post([&ai]() { ai.Idle(); });
			}
		}
	}

	void CreatureAIResetState::OnLeave()
	{
		auto& controlled = GetControlled();

		m_onHomeReached.disconnect();
		controlled.RaiseTrigger(trigger_event::OnReachedHome);

		// Consumed — the idle state will now use the saved waypoint index to resume.
		GetAI().ClearSavedPatrolReturnPosition();

		// Fully heal unit
		if (controlled.IsAlive())
		{
			controlled.Set<uint32>(object_fields::Health, controlled.GetMaxHealth());

			// Also restore full mana / energy if applicable
			if (controlled.GetPowerType() == power_type::Mana)
			{
				controlled.Set<uint32>(object_fields::Mana, controlled.GetMaxPower());
			}
			else if (controlled.GetPowerType() == power_type::Energy)
			{
				controlled.Set<uint32>(object_fields::Energy, controlled.GetMaxPower());
			}
			else if (controlled.GetPowerType() == power_type::Rage)
			{
				// Rage is reset to 0 on reset
				controlled.Set<uint32>(object_fields::Rage, 0);
			}
		}

		CreatureAIState::OnLeave();
	}
}
