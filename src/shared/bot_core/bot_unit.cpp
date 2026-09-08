// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_unit.h"

#include <algorithm>

namespace mmo
{
	BotUnit::BotUnit(const uint64 guid, const ObjectTypeId typeId)
		: m_guid(guid)
		, m_typeId(typeId)
	{
		// Initialize default speeds
		m_speeds[static_cast<size_t>(MovementType::Walk)] = 2.5f;
		m_speeds[static_cast<size_t>(MovementType::Run)] = 7.0f;
		m_speeds[static_cast<size_t>(MovementType::Backwards)] = 4.5f;
		m_speeds[static_cast<size_t>(MovementType::Swim)] = 4.722222f;
		m_speeds[static_cast<size_t>(MovementType::SwimBackwards)] = 2.5f;
		m_speeds[static_cast<size_t>(MovementType::Turn)] = 3.141593f;
		m_speeds[static_cast<size_t>(MovementType::Flight)] = 7.0f;
		m_speeds[static_cast<size_t>(MovementType::FlightBackwards)] = 4.5f;
	}

	float BotUnit::GetSpeed(const MovementType type) const
	{
		const size_t index = static_cast<size_t>(type);
		if (index < m_speeds.size())
		{
			return m_speeds[index];
		}

		return 0.0f;
	}

	float BotUnit::GetDistanceTo(const Vector3& point) const
	{
		return (m_position - point).GetLength();
	}

	float BotUnit::GetDistanceToSquared(const Vector3& point) const
	{
		return (m_position - point).GetSquaredLength();
	}

	float BotUnit::GetDistanceTo(const BotUnit& other) const
	{
		return GetDistanceTo(other.GetPosition());
	}

	float BotUnit::GetHealthPercent() const
	{
		if (m_maxHealth == 0)
		{
			return 0.0f;
		}

		return static_cast<float>(m_health) / static_cast<float>(m_maxHealth);
	}

	float BotUnit::GetPowerPercent() const
	{
		if (m_maxPower == 0)
		{
			return 0.0f;
		}

		return static_cast<float>(m_power) / static_cast<float>(m_maxPower);
	}

	bool BotUnit::KnowsSpell(const uint32 spellId) const
	{
		return m_knownSpells.find(spellId) != m_knownSpells.end();
	}

	std::vector<uint32> BotUnit::GetKnownSpellIds() const
	{
		std::vector<uint32> spellIds;
		spellIds.reserve(m_knownSpells.size());
		for (const uint32 spellId : m_knownSpells)
		{
			spellIds.push_back(spellId);
		}

		std::sort(spellIds.begin(), spellIds.end());
		return spellIds;
	}

	bool BotUnit::HasAura(const uint32 spellId) const
	{
		return std::find_if(m_visibleAuras.begin(), m_visibleAuras.end(), [spellId](const AuraState& aura)
		{
			return aura.spellId == spellId;
		}) != m_visibleAuras.end();
	}

	bool BotUnit::IsSpellOnCooldown(const uint32 spellId, const GameTime nowMs) const
	{
		return GetSpellCooldownRemaining(spellId, nowMs) > 0;
	}

	GameTime BotUnit::GetSpellCooldownRemaining(const uint32 spellId, const GameTime nowMs) const
	{
		const auto it = m_spellCooldowns.find(spellId);
		if (it == m_spellCooldowns.end())
		{
			return 0;
		}

		if (it->second.endsAtMs <= nowMs)
		{
			return 0;
		}

		return it->second.endsAtMs - nowMs;
	}

	std::vector<BotUnit::CooldownState> BotUnit::GetActiveCooldowns(const GameTime nowMs) const
	{
		std::vector<CooldownState> cooldowns;
		cooldowns.reserve(m_spellCooldowns.size());
		for (const auto& [spellId, cooldown] : m_spellCooldowns)
		{
			if (cooldown.endsAtMs > nowMs)
			{
				cooldowns.push_back(cooldown);
			}
		}

		return cooldowns;
	}

	bool BotUnit::IsInCombat() const
	{
		// InCombat flag is 0x00000001 in unit_flags
		return (m_unitFlags & 0x00000001) != 0;
	}

	bool BotUnit::IsMoving() const
	{
		// Check for any directional movement flags
		constexpr uint32 movementMask = 
			movement_flags::Forward | 
			movement_flags::Backward | 
			movement_flags::StrafeLeft | 
			movement_flags::StrafeRight |
			movement_flags::TurnLeft |
			movement_flags::TurnRight;

		return (m_movementInfo.movementFlags & movementMask) != 0;
	}

	bool BotUnit::IsHostileTo(const BotUnit& other) const
	{
		// Simplified hostility check
		// In a full implementation, this would use faction data from proto files

		// Same unit is never hostile to itself
		if (m_guid == other.m_guid)
		{
			return false;
		}

		// Players are generally not hostile to each other in PvE
		// (would need PvP flag checking for full implementation)
		if (IsPlayer() && other.IsPlayer())
		{
			return false;
		}

		// If we're a creature targeting a player, we're likely hostile
		if (IsCreature() && other.IsPlayer())
		{
			// Creatures with friendly NPC flags are not hostile
			// Quest givers, vendors, etc.
			if (m_npcFlags != 0)
			{
				return false;
			}

			// If we're targeting them, we're hostile
			if (m_targetGuid == other.m_guid)
			{
				return true;
			}

			// If they're targeting us and in combat, assume hostile
			if (other.m_targetGuid == m_guid && other.IsInCombat())
			{
				return true;
			}
		}

		// If other is a creature targeting us
		if (other.IsCreature() && IsPlayer())
		{
			if (other.m_npcFlags != 0)
			{
				return false;
			}

			if (other.m_targetGuid == m_guid)
			{
				return true;
			}
		}

		// Default: assume hostile creatures with no NPC flags
		if (IsCreature() && m_npcFlags == 0)
		{
			return true;
		}

		if (other.IsCreature() && other.m_npcFlags == 0)
		{
			return true;
		}

		return false;
	}

	bool BotUnit::IsFriendlyTo(const BotUnit& other) const
	{
		// Simplified friendly check - opposite of hostile

		// Same unit is friendly to itself
		if (m_guid == other.m_guid)
		{
			return true;
		}

		// Players are friendly to each other in PvE
		if (IsPlayer() && other.IsPlayer())
		{
			return true;
		}

		// Units with NPC flags (quest givers, vendors) are typically friendly
		if (IsCreature() && m_npcFlags != 0)
		{
			return true;
		}

		if (other.IsCreature() && other.m_npcFlags != 0)
		{
			return true;
		}

		return !IsHostileTo(other);
	}

	bool BotUnit::IsAttackableBy(const BotUnit& attacker) const
	{
		// Cannot attack self
		if (m_guid == attacker.m_guid)
		{
			return false;
		}

		// Cannot attack dead units
		if (IsDead())
		{
			return false;
		}

		// Only creatures can be attacked by players in PvE
		if (!IsCreature())
		{
			return false;
		}

		// Creatures with NPC flags (vendors, quest givers, trainers) are not attackable
		if (m_npcFlags != 0)
		{
			return false;
		}

		// Any other creature without NPC flags is attackable (including neutral creatures)
		return true;
	}

	void BotUnit::SetMovementInfo(const MovementInfo& info)
	{
		m_movementInfo = info;
		m_position = info.position;
		m_facing = info.facing;
	}

	void BotUnit::SetSpeed(const MovementType type, const float speed)
	{
		const size_t index = static_cast<size_t>(type);
		if (index < m_speeds.size())
		{
			m_speeds[index] = speed;
		}
	}

	void BotUnit::SetSpeeds(const std::array<float, static_cast<size_t>(MovementType::Count)>& speeds)
	{
		m_speeds = speeds;
	}

	void BotUnit::SetPower(const PowerType powerType, const uint32 power, const uint32 maxPower)
	{
		m_powerType = powerType;
		m_power = power;
		m_maxPower = maxPower;
	}

	void BotUnit::SetKnownSpells(const std::vector<uint32>& spellIds)
	{
		m_knownSpells.clear();
		for (const uint32 spellId : spellIds)
		{
			if (spellId != 0)
			{
				m_knownSpells.insert(spellId);
			}
		}
	}

	void BotUnit::LearnSpell(const uint32 spellId)
	{
		if (spellId != 0)
		{
			m_knownSpells.insert(spellId);
		}
	}

	void BotUnit::UnlearnSpell(const uint32 spellId)
	{
		m_knownSpells.erase(spellId);
		ClearSpellCooldown(spellId);
	}

	void BotUnit::SetVisibleAuras(std::vector<AuraState> auras)
	{
		m_visibleAuras = std::move(auras);
	}

	void BotUnit::SetSpellCooldown(const uint32 spellId, const GameTime nowMs, const GameTime durationMs)
	{
		if (spellId == 0)
		{
			return;
		}

		if (durationMs == 0)
		{
			ClearSpellCooldown(spellId);
			return;
		}

		CooldownState cooldown;
		cooldown.spellId = spellId;
		cooldown.startedAtMs = nowMs;
		cooldown.durationMs = durationMs;
		cooldown.endsAtMs = nowMs + durationMs;
		m_spellCooldowns[spellId] = cooldown;
	}

	void BotUnit::ClearSpellCooldown(const uint32 spellId)
	{
		m_spellCooldowns.erase(spellId);
	}

	void BotUnit::SetLastCastPending(const uint32 spellId, const uint32 targetFlags, const uint64 unitTargetGuid, const GameTime nowMs)
	{
		SetLastCastState(CastState::Status::Pending, spellId, targetFlags, unitTargetGuid, nowMs);
		m_lastCastState.requestedAtMs = nowMs;
		m_lastCastState.failureReason = spell_cast_result::CastOkay;
	}

	void BotUnit::SetLastCastStarted(const uint32 spellId, const uint32 targetFlags, const uint64 unitTargetGuid, const GameTime nowMs)
	{
		SetLastCastState(CastState::Status::Started, spellId, targetFlags, unitTargetGuid, nowMs);
		if (m_lastCastState.requestedAtMs == 0)
		{
			m_lastCastState.requestedAtMs = nowMs;
		}
		m_lastCastState.failureReason = spell_cast_result::CastOkay;
	}

	void BotUnit::SetLastCastSucceeded(const uint32 spellId, const uint32 targetFlags, const uint64 unitTargetGuid, const GameTime nowMs)
	{
		SetLastCastState(CastState::Status::Succeeded, spellId, targetFlags, unitTargetGuid, nowMs);
		if (m_lastCastState.requestedAtMs == 0)
		{
			m_lastCastState.requestedAtMs = nowMs;
		}
		m_lastCastState.failureReason = spell_cast_result::CastOkay;
	}

	void BotUnit::SetLastCastFailed(const uint32 spellId, const SpellCastResult result, const GameTime nowMs)
	{
		m_lastCastState.status = CastState::Status::Failed;
		m_lastCastState.spellId = spellId;
		m_lastCastState.updatedAtMs = nowMs;
		if (m_lastCastState.requestedAtMs == 0)
		{
			m_lastCastState.requestedAtMs = nowMs;
		}
		m_lastCastState.failureReason = result;
	}

	void BotUnit::SetLastCastState(const CastState::Status status, const uint32 spellId, const uint32 targetFlags, const uint64 unitTargetGuid, const GameTime nowMs)
	{
		m_lastCastState.status = status;
		m_lastCastState.spellId = spellId;
		m_lastCastState.targetFlags = targetFlags;
		m_lastCastState.unitTargetGuid = unitTargetGuid;
		m_lastCastState.updatedAtMs = nowMs;
	}

}
