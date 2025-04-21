
#include "creature_ai.h"
#include "creature_ai_prepare_state.h"
#include "creature_ai_idle_state.h"
#include "creature_ai_combat_state.h"
#include "creature_ai_reset_state.h"
#include "creature_ai_death_state.h"
#include "objects/game_creature_s.h"
#include "objects/game_unit_s.h"
#include "world_instance.h"
#include "universe.h"

namespace mmo
{
	CreatureAI::Home::~Home()
	= default;

	CreatureAI::CreatureAI(GameCreatureS& controlled, const Home& home)
		: m_controlled(controlled)
		, m_home(home)
	{
		// Connect to spawn event
		m_onSpawned = m_controlled.spawned.connect(this, &CreatureAI::OnSpawned);
		m_onDespawned = m_controlled.despawned.connect(this, &CreatureAI::OnDespawned);
	}

	CreatureAI::~CreatureAI()
		= default;

	void CreatureAI::OnSpawned(WorldInstance& instance)
	{
		m_onKilled = m_controlled.killed.connect([this](GameUnitS* killer)
			{
				auto state = std::make_shared<CreatureAIDeathState>(*this);
				SetState(std::move(state));
			});
		m_onDamaged = m_controlled.takenDamage.connect([this](GameUnitS* attacker, uint32 school, DamageType damageType)
			{
				if (attacker) 
				{
					m_state->OnDamage(*attacker);
				}
			});

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

	void CreatureAI::EnterCombat(GameUnitS& victim)
	{
		auto state = std::make_shared<CreatureAICombatState>(*this, victim);
		SetState(std::move(state));
	}

	void CreatureAI::Reset()
	{
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

	void CreatureAI::OnThreatened(GameUnitS& threat, float amount)
	{
		GameCreatureS& controlled = GetControlled();
		if (threat.GetGuid() == controlled.GetGuid())
		{
			return;
		}

		auto* worldInstance = controlled.GetWorldInstance();
		ASSERT(worldInstance);

		if (!controlled.UnitIsFriendly(threat))
		{
			const auto& location = controlled.GetPosition();

			worldInstance->GetUnitFinder().FindUnits(Circle(location.x, location.z, 8.0f), [&controlled, &threat, &worldInstance](GameUnitS& unit) -> bool
				{
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

					// TODO: Line of sight

					if (controlled.UnitIsFriendly(unit) && controlled.UnitIsEnemy(threat))
					{
						worldInstance->GetUniverse().Post([&unit, &threat]()
							{
								unit.threatened(threat, 0.0f);
							});
					}

					return false;
				});

			// Warning: This destroys the current AI state as it enters the combat state
			EnterCombat(threat);
		}
	}

}
