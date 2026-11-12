// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_ai_pet_state.h"

#include "game_server/ai/creature_ai.h"
#include "game_server/objects/game_creature_s.h"
#include "game_server/pets/pet_behavior.h"
#include "game_server/pets/pet_manager.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"

#include <cmath>
#include <vector>

namespace mmo
{
	namespace
	{
		float FlatDistance(const Vector3& a, const Vector3& b)
		{
			const float dx = a.x - b.x;
			const float dz = a.z - b.z;
			return std::sqrt(dx * dx + dz * dz);
		}

		/// A pet closer to its destination than this counts as arrived.
		constexpr float arrivalSlack = 1.0f;

		/// The destination is replanned once the owner moved the slot further than this.
		constexpr float replanDistance = 1.0f;

		/// A pet further away from its slot than this runs instead of walking.
		constexpr float runDistance = 3.0f;
	}

	CreatureAIPetState::CreatureAIPetState(CreatureAI& ai)
		: CreatureAIState(ai)
		, m_updateCountdown(ai.GetControlled().GetTimers())
	{
	}

	CreatureAIPetState::~CreatureAIPetState() = default;

	void CreatureAIPetState::OnEnter()
	{
		CreatureAIState::OnEnter();

		auto& controlled = GetControlled();

		// Coming out of a fight: stop swinging at whatever is left.
		if (controlled.IsAttacking() || controlled.Get<uint64>(object_fields::TargetUnit) != 0)
		{
			controlled.SetTarget(0);
		}

		m_connections += m_updateCountdown.ended.connect(*this, &CreatureAIPetState::Update);
		m_connections += controlled.threatened.connect([this](GameUnitS& threatener, float)
			{
				// Warning: may destroy this state as the pet enters combat
				GetAI().OnPetAttacked(threatener);
			});

		// First step on the next tick: entering combat from inside OnEnter would replace this state
		// while CreatureAI::SetState is still activating it.
		if (WorldInstance* world = controlled.GetWorldInstance())
		{
			std::weak_ptr weakThis = shared_from_this();
			world->GetUniverse().Post([weakThis]()
				{
					if (const auto strongThis = weakThis.lock())
					{
						if (strongThis->IsActive())
						{
							static_cast<CreatureAIPetState&>(*strongThis).Update();
						}
					}
				});
		}
	}

	void CreatureAIPetState::OnLeave()
	{
		m_updateCountdown.Cancel();
		m_connections.disconnect();

		CreatureAIState::OnLeave();
	}

	void CreatureAIPetState::OnDamage(GameUnitS& attacker)
	{
		CreatureAIState::OnDamage(attacker);

		// Warning: may destroy this state as the pet enters combat
		GetAI().OnPetAttacked(attacker);
	}

	void CreatureAIPetState::Update()
	{
		// Engaging replaces (and destroys) this state; keep it alive until the frame is done.
		const auto selfHold = shared_from_this();
		if (!IsActive())
		{
			return;
		}

		auto& controlled = GetControlled();
		auto& mover = controlled.GetMover();
		if (!controlled.IsAlive() || !controlled.GetWorldInstance())
		{
			return;
		}

		m_updateCountdown.SetEnd(GetAsyncTimeMs() + PetUpdateInterval);

		const std::shared_ptr<GameUnitS> owner = GetAI().GetPetOwner();
		if (!owner || owner->GetWorldInstance() != controlled.GetWorldInstance())
		{
			// The owner is gone or on another map. Temporary pets are despawned by their owner's
			// pet manager; a persistent pet is dismissed and resummoned by its owner instead (a
			// creature cannot follow its owner across worlds). Wait for that.
			mover.StopMovement();
			m_hasDestination = false;
			return;
		}

		MatchOwnerSpeed(*owner);

		// Beyond the leash the pet first returns to its owner: the combat state would break off any
		// fight started out there right away (it leashes at the same distance).
		const bool beyondLeash = controlled.GetSquaredDistanceTo(owner->GetPosition(), false) > PetLeashDistance * PetLeashDistance;
		if (!beyondLeash && owner->IsAlive() && !GetAI().IsPetRecalled() && TryEngage(*owner))
		{
			return;
		}

		if (!IsActive() || controlled.IsUnderForcedMovement())
		{
			return;
		}

		const Vector3 current = mover.GetCurrentLocation();

		if (GetAI().IsPetStaying())
		{
			// Back to the stay position after a fight; otherwise hold still.
			const Vector3& stay = GetAI().GetPetStayPosition();
			if (FlatDistance(current, stay) > arrivalSlack && (!m_hasDestination || !mover.IsMoving()))
			{
				MoveTowards(stay, controlled.GetFacing(), true);
			}
			return;
		}

		uint32 slotIndex = 0;
		uint32 slotCount = 1;
		if (PetManager* pets = owner->FindPets())
		{
			pets->GetPetSlot(controlled, slotIndex, slotCount);
		}

		const Radian ownerFacing = owner->GetFacing();
		const Vector3 slot = PetFollowSlot(owner->GetPosition(), ownerFacing, slotIndex, slotCount);

		if (FlatDistance(current, owner->GetPosition()) > PetTeleportDistance)
		{
			mover.Teleport(slot, ownerFacing);
			m_hasDestination = false;
			return;
		}

		const float slotDistance = FlatDistance(current, slot);
		if (slotDistance <= arrivalSlack)
		{
			m_hasDestination = false;
			return;
		}

		// Only replan when the slot actually moved: MoveTo recalculates the path and broadcasts a
		// movement packet every time.
		if (!m_hasDestination || !mover.IsMoving() || FlatDistance(slot, m_lastDestination) > replanDistance)
		{
			MoveTowards(slot, ownerFacing, slotDistance > runDistance || owner->IsMoving());
		}
	}

	bool CreatureAIPetState::TryEngage(GameUnitS& owner)
	{
		CreatureAI& ai = GetAI();
		GameCreatureS& controlled = GetControlled();
		WorldInstance* world = controlled.GetWorldInstance();

		// What the pet may pick on its own. A staying pet only takes on what comes within its reach.
		const auto usable = [&](const GameUnitS* unit) -> bool
		{
			if (!unit || unit == &controlled || !unit->IsAlive() || !controlled.UnitCanAttack(*unit) || !unit->CanBeSeenBy(controlled))
			{
				return false;
			}

			// Nothing that would drag the pet beyond its leash
			if (owner.GetSquaredDistanceTo(unit->GetPosition(), false) > PetLeashDistance * PetLeashDistance)
			{
				return false;
			}

			return ai.IsWithinPetStayReach(*unit);
		};

		uint64 commanded = ai.GetPetCommandedTarget();
		if (commanded != 0)
		{
			const GameUnitS* unit = world->FindByGuid<GameUnitS>(commanded);
			if (!unit || !unit->IsAlive() || !controlled.UnitCanAttack(*unit))
			{
				ai.ClearPetCommandedTarget();
				commanded = 0;
			}
		}

		// A pet that only fights its owner's target takes what the owner attacked last and leaves
		// the units attacking its owner or itself alone.
		const bool ownerTargetOnly = ai.IsPetOwnerTargetOnly();
		const GameUnitS* ownerTarget = ownerTargetOnly ? ai.FindPetOwnerTarget() : owner.GetVictim();
		const uint64 ownerVictim = usable(ownerTarget) ? ownerTarget->GetGuid() : 0;

		std::vector<uint64> ownerAttackers;
		std::vector<uint64> selfAttackers;
		if (!ownerTargetOnly)
		{
			owner.ForEachAttacker([&](const GameUnitS& attacker)
				{
					if (usable(&attacker))
					{
						ownerAttackers.push_back(attacker.GetGuid());
					}
				});

			controlled.ForEachAttacker([&](const GameUnitS& attacker)
				{
					if (usable(&attacker))
					{
						selfAttackers.push_back(attacker.GetGuid());
					}
				});
		}

		const uint64 targetGuid = ChoosePetTarget(commanded, ownerVictim, ownerAttackers, selfAttackers, ai.GetPetStrategy());
		if (targetGuid == 0)
		{
			return false;
		}

		GameUnitS* target = world->FindByGuid<GameUnitS>(targetGuid);
		if (!target)
		{
			return false;
		}

		// Warning: This destroys the current AI state as it enters the combat state
		ai.EngagePetTarget(*target);
		return !IsActive();
	}

	void CreatureAIPetState::MoveTowards(const Vector3& destination, const Radian& facing, const bool run)
	{
		auto& controlled = GetControlled();
		if (controlled.IsRooted() || controlled.IsUnderForcedMovement())
		{
			return;
		}

		controlled.SetMovementMode(run ? unit_movement_mode::Run : unit_movement_mode::Walk);

		auto& mover = controlled.GetMover();
		if (mover.MoveTo(destination, 0.25f, &facing))
		{
			m_lastDestination = destination;
			m_hasDestination = true;
			return;
		}

		// No path (off the navigation mesh, across a gap): the pet joins its owner anyway.
		m_hasDestination = false;
		if (FlatDistance(mover.GetCurrentLocation(), destination) > PetNoPathTeleportDistance)
		{
			mover.Teleport(destination, facing);
		}
	}

	void CreatureAIPetState::MatchOwnerSpeed(const GameUnitS& owner)
	{
		auto& controlled = GetControlled();
		const float naturalBase = GetAI().GetPetNaturalRunBaseSpeed();
		if (naturalBase <= 0.0f)
		{
			return;
		}

		// The base speed is raised: the pet's own speed auras (a snare) still apply on top of it.
		const float base = PetRunSpeed(naturalBase, owner.GetSpeed(movement_type::Run));
		if (std::fabs(base - controlled.GetBaseSpeed(movement_type::Run)) > 0.01f)
		{
			controlled.SetBaseSpeed(movement_type::Run, base);
		}
	}
}
