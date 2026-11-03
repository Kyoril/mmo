// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "creature_ai.h"
#include "creature_ai_prepare_state.h"
#include "creature_ai_idle_state.h"
#include "creature_ai_alert_state.h"
#include "creature_ai_combat_state.h"
#include "creature_ai_reset_state.h"
#include "creature_ai_death_state.h"
#include "creature_combat_script.h"
#include "objects/game_creature_s.h"
#include "objects/game_unit_s.h"
#include "game_server/world/world_instance.h"
#include "game_server/world/universe.h"
#include "creature_ai_pet_state.h"
#include "game_server/pets/pet_behavior.h"
#include "game_server/pets/pet_manager.h"
#include "shared/proto_data/units.pb.h"

namespace mmo
{
	CreatureAI::Home::~Home()
	= default;

	CreatureAI::CreatureAI(GameCreatureS& controlled, const Home& home)
		: m_controlled(controlled)
		, m_home(home)
		, m_evading(false)
		, m_savedPatrolReturnPosition(Vector3::Zero)
		, m_hasPatrolReturnPosition(false)
		, m_savedPatrolWaypointIndex(0)
		, m_hasPatrolWaypointSaved(false)
	{
		// Connect to spawn event
		m_onSpawned = m_controlled.spawned.connect(this, &CreatureAI::OnSpawned);
		m_onDespawned = m_controlled.despawned.connect(this, &CreatureAI::OnDespawned);
	}

	CreatureAI::~CreatureAI()
		= default;

	void CreatureAI::OnSpawned(WorldInstance& instance)
	{
		// Clear any saved patrol state from a previous life so the creature starts fresh.
		m_hasPatrolReturnPosition = false;
		m_savedPatrolReturnPosition = Vector3::Zero;
		m_hasPatrolWaypointSaved = false;
		m_savedPatrolWaypointIndex = 0;

		// A combat script that forbids dying (training dummies) is honoured before the death,
		// through GameCreatureS::CanDie, so a creature that gets here really died.
		m_onKilled = m_controlled.killed.connect([this](GameUnitS* killer)
			{
				auto state = std::make_shared<CreatureAIDeathState>(*this);
				SetState(std::move(state));
			});
		m_onDamaged = m_controlled.takenDamage.connect([this](GameUnitS* attacker, uint32 school, DamageType damageType)
			{
				// m_state is null once the creature has despawned - OnDespawned clears it - and this
				// signal can still arrive afterwards, because a swing already in flight resolves
				// against the unit after it has gone. Found by the bot swarm, which kills things fast
				// enough for the two to overlap.
				if (attacker && m_state)
				{
					m_state->OnDamage(*attacker);
				}
			});

		// A pet that comes back into the world (respawned) goes straight back to its owner.
		if (m_petMode)
		{
			SetState(std::make_shared<CreatureAIPetState>(*this));
			return;
		}

		// Enter the preparation state
		auto state = std::make_shared<CreatureAIPrepareState>(*this);
		SetState(std::move(state));
	}

	void CreatureAI::OnDespawned(GameObjectS&)
	{
		SetState(nullptr);
	}

	void CreatureAI::Idle()
	{
		// Owned creatures have no idle: they are with their owner.
		if (m_petMode)
		{
			SetState(std::make_shared<CreatureAIPetState>(*this));
			return;
		}

		auto state = std::make_shared<CreatureAIIdleState>(*this);
		SetState(std::move(state));
	}

	void CreatureAI::SetState(CreatureAI::CreatureAIStatePtr state)
	{
		if (m_state)
		{
			m_state->OnLeave();
			m_state.reset();
		}

		// Every state change causes the creature to leave evade mode
		m_evading = false;

		if (state)
		{
			m_state = std::move(state);
			m_state->OnEnter();
		}
	}

	GameCreatureS& CreatureAI::GetControlled() const
	{
		return m_controlled;
	}

	const CreatureAI::Home& CreatureAI::GetHome() const
	{
		return m_home;
	}

	bool CreatureAI::CanControlledDie() const
	{
		if (const auto* combatState = dynamic_cast<const CreatureAICombatState*>(m_state.get()))
		{
			if (const CreatureCombatScript* script = combatState->GetScript())
			{
				return script->CanDie();
			}
		}

		return true;
	}

	void CreatureAI::EnterCombat(GameUnitS& victim)
	{
		auto state = std::make_shared<CreatureAICombatState>(*this, victim);
		SetState(std::move(state));
	}

	void CreatureAI::EnterAlert(GameUnitS& target)
	{
		// Don't restart the alert when we are already alerted (would reset the timer and
		// re-trigger the client notification every scan interval).
		if (dynamic_cast<CreatureAIAlertState*>(m_state.get()) != nullptr)
		{
			return;
		}

		auto state = std::make_shared<CreatureAIAlertState>(*this, target);
		SetState(std::move(state));
	}

	void CreatureAI::Reset()
	{
		// A pet does not reset home or evade-heal: when its fight is over it returns to its owner.
		if (m_petMode)
		{
			SetState(std::make_shared<CreatureAIPetState>(*this));
			return;
		}

		// The pets a creature summoned for its fight go away with the fight.
		if (PetManager* pets = m_controlled.FindPets())
		{
			pets->DespawnTemporaryPets();
		}

		auto state = std::make_shared<CreatureAIResetState>(*this);
		SetState(std::move(state));

		// We are now evading
		m_evading = true;
	}

	void CreatureAI::OnCombatMovementChanged()
	{
		if (m_state)
		{
			m_state->OnCombatMovementChanged();
		}
	}

	void CreatureAI::OnCreatureMovementChanged()
	{
		if (m_state)
		{
			m_state->OnCreatureMovementChanged();
		}
	}

	void CreatureAI::OnControlledMoved()
	{
		if (m_state)
		{
			m_state->OnControlledMoved();
		}
	}

	void CreatureAI::SetHome(Home home)
	{
		m_home = std::move(home);
	}

	void CreatureAI::SavePatrolReturnPosition(const Vector3& position)
	{
		m_savedPatrolReturnPosition = position;
		m_hasPatrolReturnPosition = true;
	}

	const Vector3& CreatureAI::GetSavedPatrolReturnPosition() const
	{
		return m_savedPatrolReturnPosition;
	}

	bool CreatureAI::HasSavedPatrolReturnPosition() const
	{
		return m_hasPatrolReturnPosition;
	}

	void CreatureAI::ClearSavedPatrolReturnPosition()
	{
		m_hasPatrolReturnPosition = false;
		m_savedPatrolReturnPosition = Vector3::Zero;
	}

	void CreatureAI::SavePatrolWaypointIndex(const size_t index)
	{
		m_savedPatrolWaypointIndex = index;
		m_hasPatrolWaypointSaved = true;
	}

	size_t CreatureAI::GetSavedPatrolWaypointIndex() const
	{
		return m_savedPatrolWaypointIndex;
	}

	bool CreatureAI::HasSavedPatrolWaypointIndex() const
	{
		return m_hasPatrolWaypointSaved;
	}

	void CreatureAI::ClearSavedPatrolWaypointIndex()
	{
		m_hasPatrolWaypointSaved = false;
		m_savedPatrolWaypointIndex = 0;
	}

	void CreatureAI::OnThreatened(GameUnitS& threat, float amount)
	{
		GameCreatureS& controlled = GetControlled();
		if (threat.GetGuid() == controlled.GetGuid())
		{
			return;
		}

		if (!controlled.UnitIsFriendly(threat))
		{
			CallForHelp(threat);

			// Warning: This destroys the current AI state as it enters the combat state
			EnterCombat(threat);
		}
	}

	void CreatureAI::CallForHelp(GameUnitS& threat)
	{
		GameCreatureS& controlled = GetControlled();
		if (threat.GetGuid() == controlled.GetGuid() || controlled.UnitIsFriendly(threat))
		{
			return;
		}

		auto* worldInstance = controlled.GetWorldInstance();
		ASSERT(worldInstance);

		const auto& location = controlled.GetPosition();

		worldInstance->GetUnitFinder().FindUnits(Circle(location.x, location.z, 8.0f), [&controlled, &threat, &worldInstance](GameUnitS& unit) -> bool
			{
				if (&unit == &controlled)
				{
					return true;
				}

				if (!unit.IsUnit())
				{
					return true;
				}

				if (!unit.IsAlive())
				{
					return true;
				}

				if (unit.IsInCombat())
				{
					return true;
				}

				// Allies behind walls or floors do not hear the call
				if (MapData* mapData = worldInstance->GetMapData())
				{
					if (!mapData->IsInLineOfSight(controlled.GetPosition(), unit.GetPosition()))
					{
						return true;
					}
				}

				auto* ally = dynamic_cast<GameCreatureS*>(&unit);
				if (ally == nullptr)
				{
					return true;
				}

				if (controlled.UnitIsFriendly(unit) && controlled.UnitIsEnemy(threat))
				{
					auto strongAlly = std::static_pointer_cast<GameCreatureS>(ally->shared_from_this());
					auto strongThreat = std::static_pointer_cast<GameUnitS>(threat.shared_from_this());
					worldInstance->GetUniverse().Post([strongAlly, strongThreat]()
						{
							if (CreatureAI* allyAI = strongAlly->GetAI())
							{
								allyAI->OnCalledForHelp(*strongThreat);
							}
						});
				}

				// Keep searching: every ally in range is called, not just the first unit found
				return true;
			});
	}

	void CreatureAI::OnCalledForHelp(GameUnitS& threat)
	{
		// Only creatures waiting for a fight answer the call; evading, alerted or dead ones keep their state
		if (dynamic_cast<CreatureAIIdleState*>(m_state.get()) == nullptr && dynamic_cast<CreatureAIPrepareState*>(m_state.get()) == nullptr)
		{
			return;
		}

		GameCreatureS& controlled = GetControlled();
		if (!controlled.IsAlive() || controlled.IsInCombat() || threat.GetGuid() == controlled.GetGuid())
		{
			return;
		}

		if (!threat.IsAlive() || !controlled.UnitIsEnemy(threat))
		{
			return;
		}

		// No call for help of our own: a call reaches one wave of allies, or packed camps chain-pull.
		// Warning: This destroys the current AI state as it enters the combat state
		EnterCombat(threat);
	}

	void CreatureAI::EnterPetMode(GameUnitS& owner)
	{
		m_petMode = true;
		m_petOwner = std::static_pointer_cast<GameUnitS>(owner.shared_from_this());
		m_petStrategy = pet_strategy::Assist;
		m_petStaying = false;
		m_petCommandedTarget = 0;
		m_petRecalled = false;
		m_petNaturalRunBaseSpeed = m_controlled.GetBaseSpeed(movement_type::Run);

		// Temporary pets (a swarm called into a fight) only fight what their owner attacks.
		m_petOwnerTargetOnly = m_controlled.GetPetGroup() != 0;

		// The pet state does the following; an escort follow target would fight it.
		m_controlled.ClearFollowedUnit();

		// A pet never evades back to full health, so it regenerates all the time instead of only in
		// combat like other creatures (see CreatureAICombatState::OnLeave).
		m_controlled.SetRegeneration(regeneration_flags::Health | regeneration_flags::Power);
		m_controlled.StartRegeneration();

		SetState(std::make_shared<CreatureAIPetState>(*this));
	}

	void CreatureAI::OnPetCommand(const pet_command::Type command, const uint64 targetGuid, const uint32 param)
	{
		if (!m_petMode || !m_controlled.IsAlive())
		{
			return;
		}

		const bool fighting = dynamic_cast<CreatureAICombatState*>(m_state.get()) != nullptr;

		switch (command)
		{
		case pet_command::Attack:
			{
				WorldInstance* world = m_controlled.GetWorldInstance();
				GameUnitS* target = world ? world->FindByGuid<GameUnitS>(targetGuid) : nullptr;
				if (!target || !target->IsAlive() || !m_controlled.UnitCanAttack(*target))
				{
					return;
				}

				m_petCommandedTarget = targetGuid;
				m_petRecalled = false;
				EngagePetTarget(*target);
			}
			break;

		case pet_command::Follow:
			m_petStaying = false;
			m_petCommandedTarget = 0;
			if (fighting)
			{
				// Called back out of the fight: the pet does not run straight back into it.
				m_petRecalled = true;
				Reset();
			}
			else if (auto* petState = dynamic_cast<CreatureAIPetState*>(m_state.get()))
			{
				petState->Update();
			}
			break;

		case pet_command::Stay:
			m_petStaying = true;
			m_petStayPosition = m_controlled.GetMover().GetCurrentLocation();
			if (!fighting)
			{
				m_controlled.GetMover().StopMovement();
			}
			break;

		case pet_command::SetStrategy:
			if (param > pet_strategy::Passive)
			{
				return;
			}

			m_petStrategy = static_cast<pet_strategy::Type>(param);
			if (fighting && m_petStrategy == pet_strategy::Passive && m_petCommandedTarget == 0)
			{
				// A passive pet only fights what it is told to.
				Reset();
			}
			break;

		case pet_command::Dismiss:
		default:
			// Dismissing is up to the pet manager, which owns the pet's lifetime.
			break;
		}
	}

	void CreatureAI::OnOwnerAttack(GameUnitS& victim)
	{
		if (!m_petMode || m_petStrategy != pet_strategy::Assist)
		{
			return;
		}

		m_petRecalled = false;

		// A commanded target goes first while it lives.
		if (m_petCommandedTarget != 0)
		{
			WorldInstance* world = m_controlled.GetWorldInstance();
			const GameUnitS* commanded = world ? world->FindByGuid<GameUnitS>(m_petCommandedTarget) : nullptr;
			if (commanded && commanded->IsAlive())
			{
				return;
			}

			m_petCommandedTarget = 0;
		}

		if (IsWithinPetStayReach(victim))
		{
			EngagePetTarget(victim);
		}
	}

	void CreatureAI::OnOwnerAttacked(GameUnitS& attacker)
	{
		if (!m_petMode || m_petStrategy != pet_strategy::Assist || m_petOwnerTargetOnly)
		{
			return;
		}

		m_petRecalled = false;

		// Already fighting: the current fight (the owner's target) goes first.
		if (dynamic_cast<CreatureAIPetState*>(m_state.get()) == nullptr)
		{
			return;
		}

		if (IsWithinPetStayReach(attacker))
		{
			EngagePetTarget(attacker);
		}
	}

	void CreatureAI::OnPetAttacked(GameUnitS& attacker)
	{
		if (!m_petMode || m_petStrategy != pet_strategy::Assist || m_petOwnerTargetOnly)
		{
			return;
		}

		m_petRecalled = false;
		if (IsWithinPetStayReach(attacker))
		{
			EngagePetTarget(attacker);
		}
	}

	GameUnitS* CreatureAI::FindPetOwnerTarget() const
	{
		const auto owner = m_petOwner.lock();
		if (!m_petMode || !owner)
		{
			return nullptr;
		}

		const auto usable = [this](GameUnitS* unit)
		{
			return unit && unit != &m_controlled && unit->IsAlive() && m_controlled.UnitCanAttack(*unit) && unit->CanBeSeenBy(m_controlled);
		};

		if (const PetManager* pets = owner->FindPets())
		{
			if (GameUnitS* target = pets->GetOwnerTarget(); usable(target))
			{
				return target;
			}
		}

		GameUnitS* victim = owner->GetVictim();
		return usable(victim) ? victim : nullptr;
	}

	void CreatureAI::EngagePetTarget(GameUnitS& target)
	{
		if (!m_state || !m_controlled.IsAlive() || &target == &m_controlled || !target.IsAlive() || !m_controlled.UnitCanAttack(target))
		{
			return;
		}

		if (target.GetWorldInstance() != m_controlled.GetWorldInstance() || !target.CanBeSeenBy(m_controlled))
		{
			return;
		}

		// A pet beyond its leash returns to its owner first: the combat state would break off at once.
		if (const auto owner = m_petOwner.lock(); owner && m_controlled.GetSquaredDistanceTo(owner->GetPosition(), false) > PetLeashDistance * PetLeashDistance)
		{
			return;
		}

		if (auto* combat = dynamic_cast<CreatureAICombatState*>(m_state.get()))
		{
			combat->ReconsiderPetVictim();
			return;
		}

		if (dynamic_cast<CreatureAIPetState*>(m_state.get()) == nullptr)
		{
			// Dead, or not yet in pet mode.
			return;
		}

		// Warning: This destroys the current AI state as it enters the combat state
		EnterCombat(target);
	}

	bool CreatureAI::IsWithinPetStayReach(const GameUnitS& unit) const
	{
		if (!m_petStaying)
		{
			return true;
		}

		const float reach = m_controlled.GetMeleeReach() + unit.GetMeleeReach();
		const float dx = unit.GetPosition().x - m_petStayPosition.x;
		const float dz = unit.GetPosition().z - m_petStayPosition.z;
		return dx * dx + dz * dz <= reach * reach;
	}

	void CreatureAI::SetPetSpells(std::vector<proto::UnitSpellEntry> spells)
	{
		m_petSpells = std::move(spells);
	}
}
