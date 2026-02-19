// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "creature_combat_script.h"
#include "creature_ai_combat_state.h"
#include "creature_ai.h"
#include "objects/game_creature_s.h"
#include "game_server/world/universe.h"
#include "game/spell_target_map.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"

#include "base/utilities.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	CreatureCombatScript::CreatureCombatScript(CreatureAICombatState& combatState)
		: m_combatState(combatState)
		, m_currentPhase(0)
	{
	}

	CreatureCombatScript::~CreatureCombatScript() = default;

	// =========================================================================
	// Default virtual implementations (all no-ops / default returns)
	// =========================================================================

	void CreatureCombatScript::OnCombatStart()
	{
	}

	void CreatureCombatScript::OnCombatEnd()
	{
	}

	bool CreatureCombatScript::OnChooseAction()
	{
		return false; // Let default AI handle it
	}

	void CreatureCombatScript::OnDamageTaken(GameUnitS& attacker)
	{
	}

	void CreatureCombatScript::OnSpellCastEnded(bool succeeded)
	{
	}

	void CreatureCombatScript::OnHealthThresholdReached(uint8 percentage)
	{
	}

	void CreatureCombatScript::OnTimerExpired(uint32 timerId)
	{
	}

	void CreatureCombatScript::OnPhaseChanged(uint32 oldPhase, uint32 newPhase)
	{
	}

	void CreatureCombatScript::OnTargetDied(GameUnitS& target)
	{
	}

	bool CreatureCombatScript::CanDie() const
	{
		return true;
	}

	bool CreatureCombatScript::CanMove() const
	{
		return true;
	}

	bool CreatureCombatScript::ShouldAutoAttack() const
	{
		return true;
	}

	bool CreatureCombatScript::ShouldResetCombat() const
	{
		// By default, don't override the normal reset logic
		return false;
	}

	GameUnitS* CreatureCombatScript::SelectSpellTarget(const proto::SpellEntry& spell)
	{
		return nullptr; // Use default targeting
	}

	// =========================================================================
	// Actions - Spells
	// =========================================================================

	bool CreatureCombatScript::CastSpellOn(uint32 spellId, GameUnitS& target)
	{
		auto& creature = GetCreature();
		const auto* spell = creature.GetProject().spells.getById(spellId);
		if (!spell)
		{
			WLOG("CombatScript: Spell " << spellId << " not found");
			return false;
		}

		SpellTargetMap targetMap;
		targetMap.SetTargetMap(spell_cast_target_flags::Unit);
		targetMap.SetUnitTarget(target.GetGuid());

		const auto result = creature.CastSpell(targetMap, *spell, spell->casttime());
		return result == spell_cast_result::CastOkay;
	}

	bool CreatureCombatScript::CastSpellOnSelf(uint32 spellId)
	{
		auto& creature = GetCreature();
		return CastSpellOn(spellId, creature);
	}

	bool CreatureCombatScript::CastSpellOnVictim(uint32 spellId)
	{
		auto* victim = GetCurrentVictim();
		if (!victim)
		{
			return false;
		}

		return CastSpellOn(spellId, *victim);
	}

	// =========================================================================
	// Actions - Movement
	// =========================================================================

	void CreatureCombatScript::MoveTo(const Vector3& position, float range)
	{
		auto& creature = GetCreature();
		creature.GetMover().MoveTo(position, range);
	}

	void CreatureCombatScript::StopMovement()
	{
		auto& creature = GetCreature();
		creature.GetMover().StopMovement();
	}

	// =========================================================================
	// Actions - Chat
	// =========================================================================

	void CreatureCombatScript::Say(const String& text)
	{
		auto& creature = GetCreature();
		creature.ChatSay(text);
	}

	void CreatureCombatScript::Yell(const String& text)
	{
		auto& creature = GetCreature();
		creature.ChatYell(text);
	}

	// =========================================================================
	// Actions - State Modification
	// =========================================================================

	void CreatureCombatScript::SetImmune(bool immune)
	{
		auto& creature = GetCreature();
		if (immune)
		{
			creature.AddFlag<uint32>(object_fields::Flags, unit_flags::Immune);
		}
		else
		{
			creature.RemoveFlag<uint32>(object_fields::Flags, unit_flags::Immune);
		}
	}

	void CreatureCombatScript::SetNotAttackable(bool notAttackable)
	{
		auto& creature = GetCreature();
		if (notAttackable)
		{
			creature.AddFlag<uint32>(object_fields::Flags, unit_flags::NotAttackable);
		}
		else
		{
			creature.RemoveFlag<uint32>(object_fields::Flags, unit_flags::NotAttackable);
		}
	}

	void CreatureCombatScript::StopAutoAttack()
	{
		auto& creature = GetCreature();
		creature.StopAttack();
	}

	void CreatureCombatScript::StartAutoAttack(GameUnitS& target)
	{
		auto& creature = GetCreature();
		creature.StartAttack(std::static_pointer_cast<GameUnitS>(target.shared_from_this()));
	}

	void CreatureCombatScript::SetVirtualEquipment(uint8 slot, uint32 displayId)
	{
		// TODO: Implement when virtual equipment display object fields are added.
		// For now, equipment changes are visual-only and need additional object field support.
		DLOG("CombatScript: SetVirtualEquipment slot=" << static_cast<int>(slot)
			<< " displayId=" << displayId << " (not yet implemented)");
	}

	void CreatureCombatScript::SetHealthPercent(uint8 percent)
	{
		auto& creature = GetCreature();

		const uint32 maxHealth = creature.GetMaxHealth();
		const uint32 newHealth = std::max<uint32>(1, (maxHealth * percent) / 100);
		creature.Set<uint32>(object_fields::Health, newHealth);
	}

	// =========================================================================
	// Phase Management
	// =========================================================================

	uint32 CreatureCombatScript::GetPhase() const
	{
		return m_currentPhase;
	}

	void CreatureCombatScript::SetPhase(uint32 phase)
	{
		if (m_currentPhase == phase)
		{
			return;
		}

		const uint32 oldPhase = m_currentPhase;
		m_currentPhase = phase;
		OnPhaseChanged(oldPhase, phase);
	}

	// =========================================================================
	// Timer Management
	// =========================================================================

	void CreatureCombatScript::ScheduleTimer(uint32 timerId, uint32 delayMs)
	{
		// Cancel existing timer with same ID if any
		CancelTimer(timerId);

		auto& creature = GetCreature();
		auto timer = std::make_unique<Countdown>(creature.GetTimers());

		timer->ended.connect([this, timerId]()
		{
			// Remove the timer from the map before firing the callback
			// to allow re-scheduling the same timer ID inside the callback
			m_timers.erase(timerId);
			OnTimerExpired(timerId);
		});

		timer->SetEnd(GetAsyncTimeMs() + delayMs);
		m_timers[timerId] = std::move(timer);
	}

	void CreatureCombatScript::CancelTimer(uint32 timerId)
	{
		const auto it = m_timers.find(timerId);
		if (it != m_timers.end())
		{
			m_timers.erase(it);
		}
	}

	void CreatureCombatScript::CancelAllTimers()
	{
		m_timers.clear();
	}

	// =========================================================================
	// Health Threshold Management
	// =========================================================================

	void CreatureCombatScript::RegisterHealthThreshold(uint8 percentage)
	{
		if (percentage > 0 && percentage < 100)
		{
			m_healthThresholds.insert(percentage);
		}
	}

	void CreatureCombatScript::CheckHealthThresholds()
	{
		const float currentPercent = GetHealthPercent();

		// Check thresholds from highest to lowest
		// Using reverse iteration since set is sorted ascending
		for (auto it = m_healthThresholds.rbegin(); it != m_healthThresholds.rend(); ++it)
		{
			const uint8 threshold = *it;

			// Skip already-fired thresholds
			if (m_firedThresholds.contains(threshold))
			{
				continue;
			}

			// Fire if we're at or below the threshold
			if (currentPercent <= static_cast<float>(threshold))
			{
				m_firedThresholds.insert(threshold);
				OnHealthThresholdReached(threshold);
			}
		}
	}

	// =========================================================================
	// Target Selection Helpers
	// =========================================================================

	GameUnitS* CreatureCombatScript::GetCurrentVictim() const
	{
		return GetCreature().GetVictim();
	}

	GameUnitS* CreatureCombatScript::GetFurthestPlayer() const
	{
		const auto targets = GetAllThreatTargets();
		const auto& creaturePos = GetCreature().GetPosition();

		GameUnitS* furthest = nullptr;
		float maxDistSq = -1.0f;

		for (auto* target : targets)
		{
			if (!target->IsPlayer())
			{
				continue;
			}

			const float distSq = target->GetSquaredDistanceTo(creaturePos, true);
			if (distSq > maxDistSq)
			{
				maxDistSq = distSq;
				furthest = target;
			}
		}

		return furthest;
	}

	GameUnitS* CreatureCombatScript::GetClosestPlayer() const
	{
		const auto targets = GetAllThreatTargets();
		const auto& creaturePos = GetCreature().GetPosition();

		GameUnitS* closest = nullptr;
		float minDistSq = std::numeric_limits<float>::max();

		for (auto* target : targets)
		{
			if (!target->IsPlayer())
			{
				continue;
			}

			const float distSq = target->GetSquaredDistanceTo(creaturePos, true);
			if (distSq < minDistSq)
			{
				minDistSq = distSq;
				closest = target;
			}
		}

		return closest;
	}

	GameUnitS* CreatureCombatScript::GetRandomPlayer() const
	{
		const auto targets = GetAllThreatTargets();

		// Collect only players
		std::vector<GameUnitS*> players;
		for (auto* target : targets)
		{
			if (target->IsPlayer())
			{
				players.push_back(target);
			}
		}

		if (players.empty())
		{
			return nullptr;
		}

		std::uniform_int_distribution<size_t> dist(0, players.size() - 1);
		return players[dist(randomGenerator)];
	}

	GameUnitS* CreatureCombatScript::GetPlayerBehind() const
	{
		const auto targets = GetAllThreatTargets();
		const auto& creature = GetCreature();
		const auto& creaturePos = creature.GetPosition();
		const float creatureFacing = creature.GetFacing().GetValueRadians();

		// "Behind" is defined as within a 90-degree cone behind the creature
		// (135 to 225 degrees from facing direction)
		constexpr float behindAngle = 3.14159f; // Pi = directly behind
		constexpr float behindTolerance = 1.5708f; // Pi/2 = 90 degree cone

		GameUnitS* bestBehind = nullptr;
		float closestAngleDiff = behindTolerance;

		for (auto* target : targets)
		{
			if (!target->IsPlayer())
			{
				continue;
			}

			const auto& targetPos = target->GetPosition();
			const float dx = targetPos.x - creaturePos.x;
			const float dz = targetPos.z - creaturePos.z;
			const float angleToTarget = std::atan2(dz, dx);

			// Angle difference from "directly behind"
			float angleDiff = std::abs(angleToTarget - creatureFacing - behindAngle);

			// Normalize to [0, Pi]
			while (angleDiff > 3.14159f)
			{
				angleDiff -= 2.0f * 3.14159f;
			}
			angleDiff = std::abs(angleDiff);

			if (angleDiff < closestAngleDiff)
			{
				closestAngleDiff = angleDiff;
				bestBehind = target;
			}
		}

		return bestBehind;
	}

	std::vector<GameUnitS*> CreatureCombatScript::GetPlayersInRange(float range) const
	{
		const auto targets = GetAllThreatTargets();
		const auto& creaturePos = GetCreature().GetPosition();
		const float rangeSq = range * range;

		std::vector<GameUnitS*> result;
		for (auto* target : targets)
		{
			if (!target->IsPlayer())
			{
				continue;
			}

			if (target->GetSquaredDistanceTo(creaturePos, true) <= rangeSq)
			{
				result.push_back(target);
			}
		}

		return result;
	}

	std::vector<GameUnitS*> CreatureCombatScript::GetAllThreatTargets() const
	{
		return m_combatState.GetThreatTargets();
	}

	// =========================================================================
	// State Queries
	// =========================================================================

	GameCreatureS& CreatureCombatScript::GetCreature() const
	{
		return m_combatState.GetControlled();
	}

	float CreatureCombatScript::GetHealthPercent() const
	{
		const auto& creature = GetCreature();
		const uint32 health = creature.Get<uint32>(object_fields::Health);
		const uint32 maxHealth = creature.GetMaxHealth();

		if (maxHealth == 0)
		{
			return 0.0f;
		}

		return (static_cast<float>(health) / static_cast<float>(maxHealth)) * 100.0f;
	}

	bool CreatureCombatScript::IsMoving() const
	{
		return GetCreature().GetMover().IsMoving();
	}

	bool CreatureCombatScript::IsCasting() const
	{
		return m_combatState.IsCasting();
	}

	uint32 CreatureCombatScript::GetThreatCount() const
	{
		return m_combatState.GetThreatCount();
	}

	float CreatureCombatScript::GetDistanceToHome() const
	{
		const auto& creature = GetCreature();
		const auto& homePos = m_combatState.GetAI().GetHome().position;
		return std::sqrt(creature.GetSquaredDistanceTo(homePos, false));
	}

	float CreatureCombatScript::GetDistanceToTarget(const GameUnitS& target) const
	{
		const auto& creature = GetCreature();
		return std::sqrt(creature.GetSquaredDistanceTo(target.GetPosition(), true));
	}

	// =========================================================================
	// Threat Management
	// =========================================================================

	void CreatureCombatScript::ModifyThreat(GameUnitS& target, float amount)
	{
		m_combatState.AddThreatFromScript(target, amount);
	}

	void CreatureCombatScript::ResetAllThreat()
	{
		m_combatState.ResetAllThreatFromScript();
	}

	CreatureAICombatState& CreatureCombatScript::GetCombatState() const
	{
		return m_combatState;
	}
}
