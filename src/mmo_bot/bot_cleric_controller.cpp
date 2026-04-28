// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_cleric_controller.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace
{
	using namespace mmo;

	constexpr float kSelfEmergencyThreshold = 0.25f;
	constexpr float kAllyEmergencyThreshold = 0.25f;
	constexpr float kTankTriageThreshold = 0.70f;
	constexpr float kAllyTriageThreshold = 0.55f;
	constexpr float kSafePartyHealthThreshold = 0.85f;
	constexpr float kSafeSelfHealthThreshold = 0.90f;
	constexpr float kManaConservePercent = 0.20f;

	[[nodiscard]] bool IsHealthPercentValid(const float value) noexcept
	{
		return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
	}

	[[nodiscard]] bool AuraActive(const BotClericUnitSnapshot& unit, const std::optional<BotClericResolvedSpell>& spell)
	{
		return spell.has_value() && unit.activeAuraSpellIds.find(spell->spellId) != unit.activeAuraSpellIds.end();
	}

	[[nodiscard]] bool CooldownBlocked(const BotClericDecisionInput& input, const std::optional<BotClericResolvedSpell>& spell)
	{
		return spell.has_value() && input.cooldownBlockedSpellIds.find(spell->spellId) != input.cooldownBlockedSpellIds.end();
	}

	[[nodiscard]] bool CanAfford(const BotClericDecisionInput& input, const std::optional<BotClericResolvedSpell>& spell)
	{
		return spell.has_value() && input.mana >= spell->powerCost;
	}

	[[nodiscard]] bool HasValidHostileTarget(const BotClericDecisionInput& input) noexcept
	{
		return input.hasHostileTarget && input.hostileTargetGuid != 0 && input.hostileTargetAlive && input.hostileTargetHostile;
	}

	[[nodiscard]] bool CanCastOnSelf(const BotClericDecisionInput& input, const std::optional<BotClericResolvedSpell>& spell)
	{
		return spell.has_value() && !CooldownBlocked(input, spell) && CanAfford(input, spell);
	}

	[[nodiscard]] bool CanCastOnUnit(const BotClericDecisionInput& input, const std::optional<BotClericResolvedSpell>& spell, const BotClericUnitSnapshot& unit)
	{
		if (!spell.has_value() || CooldownBlocked(input, spell) || !CanAfford(input, spell))
		{
			return false;
		}

		if (!unit.isAware || !unit.isAlive || unit.guid == 0)
		{
			return false;
		}

		return spell->IsInRange(unit.distanceToSelf);
	}

	[[nodiscard]] bool CanCastAtHostile(const BotClericDecisionInput& input, const std::optional<BotClericResolvedSpell>& spell)
	{
		return spell.has_value()
			&& HasValidHostileTarget(input)
			&& !CooldownBlocked(input, spell)
			&& CanAfford(input, spell)
			&& spell->IsInRange(input.hostileTargetDistance);
	}

	[[nodiscard]] BotClericDecision Hold(std::string reason)
	{
		BotClericDecision decision;
		decision.type = BotClericDecisionType::Hold;
		decision.reason = std::move(reason);
		return decision;
	}

	[[nodiscard]] BotClericDecision CastOnSelf(const BotClericResolvedSpell& spell, std::string reason)
	{
		BotClericDecision decision;
		decision.type = BotClericDecisionType::CastSpell;
		decision.reason = std::move(reason);
		decision.spellId = spell.spellId;
		decision.castOnSelf = true;
		return decision;
	}

	[[nodiscard]] BotClericDecision CastOnUnit(const BotClericResolvedSpell& spell, const BotClericUnitSnapshot& unit, std::string reason)
	{
		BotClericDecision decision;
		decision.type = BotClericDecisionType::CastSpell;
		decision.reason = std::move(reason);
		decision.spellId = spell.spellId;
		decision.castTargetGuid = unit.guid;
		return decision;
	}

	[[nodiscard]] BotClericDecision CastAtHostile(const BotClericDecisionInput& input, const BotClericResolvedSpell& spell, std::string reason)
	{
		BotClericDecision decision;
		decision.type = BotClericDecisionType::CastSpell;
		decision.reason = std::move(reason);
		decision.spellId = spell.spellId;
		decision.castTargetGuid = input.hostileTargetGuid;
		return decision;
	}

	[[nodiscard]] std::vector<const BotClericUnitSnapshot*> CollectCandidateAllies(const BotClericDecisionInput& input, bool* foundOutOfAwarenessInjury = nullptr)
	{
		std::vector<const BotClericUnitSnapshot*> allies;
		if (foundOutOfAwarenessInjury)
		{
			*foundOutOfAwarenessInjury = false;
		}

		for (const BotClericUnitSnapshot& member : input.partyMembers)
		{
			if (member.guid == 0 || member.guid == input.self.guid || !member.isAlive)
			{
				continue;
			}
			if (!IsHealthPercentValid(member.healthPercent))
			{
				continue;
			}
			if (!member.isAware)
			{
				if (foundOutOfAwarenessInjury && member.healthPercent < kSafePartyHealthThreshold)
				{
					*foundOutOfAwarenessInjury = true;
				}
				continue;
			}
			allies.push_back(&member);
		}

		auto priority = [](const BotClericUnitSnapshot* left, const BotClericUnitSnapshot* right)
		{
			const int leftRole = left->role == BotCompanionRole::Tank ? 0 : 1;
			const int rightRole = right->role == BotCompanionRole::Tank ? 0 : 1;
			if (leftRole != rightRole)
			{
				return leftRole < rightRole;
			}
			if (left->healthPercent != right->healthPercent)
			{
				return left->healthPercent < right->healthPercent;
			}
			if (left->distanceToSelf != right->distanceToSelf)
			{
				return left->distanceToSelf < right->distanceToSelf;
			}
			return left->guid < right->guid;
		};
		std::sort(allies.begin(), allies.end(), priority);
		return allies;
	}

	[[nodiscard]] bool HasDuplicateGuids(const BotClericDecisionInput& input)
	{
		std::unordered_set<uint64> guids;
		if (input.hasSelf && input.self.guid != 0)
		{
			guids.insert(input.self.guid);
		}

		for (const BotClericUnitSnapshot& member : input.partyMembers)
		{
			if (member.guid == 0)
			{
				continue;
			}
			if (!guids.insert(member.guid).second)
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] uint32 ReserveManaCost(const BotClericCapabilities& capabilities) noexcept
	{
		if (capabilities.efficientHeal.has_value())
		{
			return capabilities.efficientHeal->powerCost;
		}
		if (capabilities.emergencyHeal.has_value())
		{
			return capabilities.emergencyHeal->powerCost;
		}
		return 0;
	}
}

namespace mmo
{
	BotClericDecision BotClericController::Evaluate(const BotClericDecisionInput& input) const
	{
		if (!input.capabilities || !input.capabilities->resolved)
		{
			return Hold("capabilities_unresolved");
		}

		switch (input.companionMode)
		{
		case BotCompanionMode::CombatAnchor:
		case BotCompanionMode::LeaderFollow:
			break;
		case BotCompanionMode::Hold:
		case BotCompanionMode::Regroup:
			return Hold("companion_hold");
		default:
			return Hold("unsupported_companion_mode");
		}

		if (!input.hasSelf || input.self.guid == 0)
		{
			return Hold("self_missing");
		}
		if (!input.self.isAlive)
		{
			return Hold("self_dead");
		}
		if (!IsHealthPercentValid(input.self.healthPercent))
		{
			return Hold("self_health_invalid");
		}
		for (const BotClericUnitSnapshot& member : input.partyMembers)
		{
			if (!IsHealthPercentValid(member.healthPercent))
			{
				return Hold("party_health_invalid");
			}
		}
		if (HasDuplicateGuids(input))
		{
			return Hold("duplicate_party_member");
		}
		if (!input.spellbookReady || !input.cooldownsReady || !input.powerReady)
		{
			return Hold("combat_state_incomplete");
		}

		const BotClericCapabilities& capabilities = *input.capabilities;
		bool foundOutOfAwarenessInjury = false;
		const std::vector<const BotClericUnitSnapshot*> allies = CollectCandidateAllies(input, &foundOutOfAwarenessInjury);

		if (input.self.healthPercent <= kSelfEmergencyThreshold)
		{
			if (CanCastOnSelf(input, capabilities.emergencyHeal))
			{
				return CastOnSelf(*capabilities.emergencyHeal, "self_emergency_heal");
			}
			if (CooldownBlocked(input, capabilities.emergencyHeal))
			{
				return Hold("self_emergency_cooldown");
			}
			return Hold("self_emergency_oom");
		}

		for (const BotClericUnitSnapshot* ally : allies)
		{
			if (ally->healthPercent > kAllyEmergencyThreshold)
			{
				continue;
			}
			if (CanCastOnUnit(input, capabilities.emergencyHeal, *ally))
			{
				return CastOnUnit(*capabilities.emergencyHeal, *ally, "ally_emergency_heal");
			}
			if (CooldownBlocked(input, capabilities.emergencyHeal))
			{
				return Hold("ally_emergency_cooldown");
			}
			if (!CanAfford(input, capabilities.emergencyHeal))
			{
				return Hold("ally_emergency_oom");
			}
			return Hold("ally_emergency_out_of_range");
		}

		for (const BotClericUnitSnapshot* ally : allies)
		{
			if (ally->role == BotCompanionRole::Tank && ally->healthPercent <= kTankTriageThreshold)
			{
				if (CanCastOnUnit(input, capabilities.efficientHeal, *ally))
				{
					return CastOnUnit(*capabilities.efficientHeal, *ally, "tank_triage_heal");
				}
				if (CooldownBlocked(input, capabilities.efficientHeal))
				{
					return Hold("tank_triage_cooldown");
				}
				if (!CanAfford(input, capabilities.efficientHeal))
				{
					return Hold("tank_triage_oom");
				}
				return Hold("tank_triage_out_of_range");
			}
		}

		for (const BotClericUnitSnapshot* ally : allies)
		{
			if (ally->healthPercent <= kAllyTriageThreshold)
			{
				if (CanCastOnUnit(input, capabilities.efficientHeal, *ally))
				{
					return CastOnUnit(*capabilities.efficientHeal, *ally, "ally_triage_heal");
				}
				if (CooldownBlocked(input, capabilities.efficientHeal))
				{
					return Hold("ally_triage_cooldown");
				}
				if (!CanAfford(input, capabilities.efficientHeal))
				{
					return Hold("ally_triage_oom");
				}
				return Hold("ally_triage_out_of_range");
			}
		}

		if (foundOutOfAwarenessInjury)
		{
			return Hold("injured_ally_out_of_awareness");
		}

		if (capabilities.supportAura.has_value() && !AuraActive(input.self, capabilities.supportAura))
		{
			if (CanCastOnSelf(input, capabilities.supportAura))
			{
				return CastOnSelf(*capabilities.supportAura, "maintain_support_aura");
			}
			if (CooldownBlocked(input, capabilities.supportAura))
			{
				return Hold("support_aura_cooldown");
			}
			if (!CanAfford(input, capabilities.supportAura))
			{
				return Hold("support_aura_oom");
			}
		}

		for (const BotClericUnitSnapshot* ally : allies)
		{
			if (ally->role != BotCompanionRole::Tank)
			{
				continue;
			}
			if (capabilities.supportBuff.has_value() && !AuraActive(*ally, capabilities.supportBuff))
			{
				if (CanCastOnUnit(input, capabilities.supportBuff, *ally))
				{
					return CastOnUnit(*capabilities.supportBuff, *ally, "maintain_support_buff");
				}
				if (CooldownBlocked(input, capabilities.supportBuff))
				{
					return Hold("support_buff_cooldown");
				}
				if (!CanAfford(input, capabilities.supportBuff))
				{
					return Hold("support_buff_oom");
				}
				return Hold("support_buff_out_of_range");
			}
			break;
		}

		const float manaPercent = input.maxMana > 0 ? static_cast<float>(input.mana) / static_cast<float>(input.maxMana) : 0.0f;
		if (manaPercent < kManaConservePercent || input.mana < ReserveManaCost(capabilities))
		{
			return Hold("conserve_mana");
		}

		const bool partyStable = std::all_of(allies.begin(), allies.end(), [](const BotClericUnitSnapshot* ally)
		{
			return ally->healthPercent >= kSafePartyHealthThreshold;
		});
		const bool stableForFiller = input.self.healthPercent >= kSafeSelfHealthThreshold && partyStable;
		if (!stableForFiller)
		{
			if (foundOutOfAwarenessInjury)
			{
				return Hold("injured_ally_out_of_awareness");
			}
			return Hold("conservative_hold");
		}

		if (foundOutOfAwarenessInjury)
		{
			return Hold("injured_ally_out_of_awareness");
		}

		if (!HasValidHostileTarget(input))
		{
			return Hold("no_hostile_target");
		}
		if (CanCastAtHostile(input, capabilities.damageFiller))
		{
			return CastAtHostile(input, *capabilities.damageFiller, "safe_damage_filler");
		}
		if (CooldownBlocked(input, capabilities.damageFiller))
		{
			return Hold("damage_filler_cooldown");
		}
		if (!CanAfford(input, capabilities.damageFiller))
		{
			return Hold("damage_filler_oom");
		}
		return Hold("damage_filler_out_of_range");
	}

	const char* ToString(const BotClericDecisionType type) noexcept
	{
		switch (type)
		{
		case BotClericDecisionType::CastSpell:
			return "cast_spell";
		case BotClericDecisionType::Hold:
		default:
			return "hold";
		}
	}
}
