// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_mage_controller.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace
{
	using namespace mmo;

	constexpr float kEmergencySpacingDistance = 8.0f;
	constexpr float kCloseRangePressureDistance = 12.0f;
	constexpr float kSelfPreservationThreshold = 0.35f;

	[[nodiscard]] bool IsValidHealthPercent(const float value) noexcept
	{
		return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
	}

	[[nodiscard]] bool IsValidDistance(const float value) noexcept
	{
		return std::isfinite(value) && value >= 0.0f;
	}

	[[nodiscard]] bool AuraActive(const BotMageSelfSnapshot& self, const std::optional<BotMageResolvedSpell>& spell)
	{
		return spell.has_value() && self.activeAuraSpellIds.find(spell->spellId) != self.activeAuraSpellIds.end();
	}

	[[nodiscard]] bool CooldownBlocked(const BotMageDecisionInput& input, const std::optional<BotMageResolvedSpell>& spell)
	{
		return spell.has_value() && input.cooldownBlockedSpellIds.find(spell->spellId) != input.cooldownBlockedSpellIds.end();
	}

	[[nodiscard]] bool CanAfford(const BotMageDecisionInput& input, const std::optional<BotMageResolvedSpell>& spell)
	{
		return spell.has_value() && input.self.mana >= spell->powerCost;
	}

	[[nodiscard]] bool HasValidPrimaryTarget(const BotMageDecisionInput& input) noexcept
	{
		return input.hasPrimaryTarget
			&& input.primaryTarget.guid != 0
			&& input.primaryTarget.isAlive
			&& input.primaryTarget.isAware
			&& input.primaryTarget.isHostile;
	}

	[[nodiscard]] bool CanCastOnSelf(const BotMageDecisionInput& input, const std::optional<BotMageResolvedSpell>& spell)
	{
		return spell.has_value() && !CooldownBlocked(input, spell) && CanAfford(input, spell);
	}

	[[nodiscard]] bool CanCastAtHostile(
		const BotMageDecisionInput& input,
		const std::optional<BotMageResolvedSpell>& spell,
		const BotMageHostileSnapshot& hostile)
	{
		if (!spell.has_value() || CooldownBlocked(input, spell) || !CanAfford(input, spell))
		{
			return false;
		}

		if (hostile.guid == 0 || !hostile.isAlive || !hostile.isAware || !hostile.isHostile || !IsValidDistance(hostile.distanceToSelf))
		{
			return false;
		}

		if (!spell->requiresTarget)
		{
			return true;
		}

		return spell->IsInRange(hostile.distanceToSelf);
	}

	[[nodiscard]] BotMageDecision Hold(std::string reason)
	{
		BotMageDecision decision;
		decision.type = BotMageDecisionType::Hold;
		decision.reason = std::move(reason);
		return decision;
	}

	[[nodiscard]] BotMageDecision CastOnSelf(
		const BotMageDecisionType type,
		const BotMageResolvedSpell& spell,
		std::string reason)
	{
		BotMageDecision decision;
		decision.type = type;
		decision.reason = std::move(reason);
		decision.spellId = spell.spellId;
		decision.castOnSelf = true;
		return decision;
	}

	[[nodiscard]] BotMageDecision CastAtHostile(
		const BotMageDecisionType type,
		const BotMageResolvedSpell& spell,
		const BotMageHostileSnapshot& hostile,
		std::string reason)
	{
		BotMageDecision decision;
		decision.type = type;
		decision.reason = std::move(reason);
		decision.spellId = spell.spellId;
		decision.castTargetGuid = hostile.guid;
		return decision;
	}

	[[nodiscard]] bool HasDuplicateHostiles(const BotMageDecisionInput& input)
	{
		std::unordered_set<uint64> guids;
		for (const BotMageHostileSnapshot& hostile : input.nearbyHostiles)
		{
			if (hostile.guid == 0)
			{
				continue;
			}
			if (!guids.insert(hostile.guid).second)
			{
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] const BotMageHostileSnapshot* FindNearestThreat(const BotMageDecisionInput& input)
	{
		const BotMageHostileSnapshot* nearest = nullptr;
		auto consider = [&](const BotMageHostileSnapshot& hostile)
		{
			if (hostile.guid == 0 || !hostile.isAlive || !hostile.isAware || !hostile.isHostile || !IsValidDistance(hostile.distanceToSelf))
			{
				return;
			}
			if (!nearest || hostile.distanceToSelf < nearest->distanceToSelf
				|| (hostile.distanceToSelf == nearest->distanceToSelf && hostile.guid < nearest->guid))
			{
				nearest = &hostile;
			}
		};

		if (HasValidPrimaryTarget(input))
		{
			consider(input.primaryTarget);
		}
		for (const BotMageHostileSnapshot& hostile : input.nearbyHostiles)
		{
			consider(hostile);
		}

		return nearest;
	}

	[[nodiscard]] bool HasAnyTargetingSelf(const BotMageDecisionInput& input) noexcept
	{
		if (HasValidPrimaryTarget(input) && input.primaryTarget.targetingSelf)
		{
			return true;
		}
		return std::any_of(input.nearbyHostiles.begin(), input.nearbyHostiles.end(), [](const BotMageHostileSnapshot& hostile)
		{
			return hostile.guid != 0 && hostile.isAlive && hostile.isAware && hostile.isHostile && hostile.targetingSelf;
		});
	}

	[[nodiscard]] std::string CapabilityHoldReason(const BotMageCapabilities& capabilities)
	{
		if (!capabilities.primaryNuke.has_value())
		{
			return "primary_nuke_missing";
		}
		return "capabilities_unresolved";
	}
}

namespace mmo
{
	BotMageDecision BotMageController::Evaluate(const BotMageDecisionInput& input) const
	{
		if (!input.capabilities)
		{
			return Hold("capabilities_unresolved");
		}

		const BotMageCapabilities& capabilities = *input.capabilities;
		if (!capabilities.primaryNuke.has_value())
		{
			return Hold(CapabilityHoldReason(capabilities));
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
		if (!IsValidHealthPercent(input.self.healthPercent))
		{
			return Hold("self_health_invalid");
		}
		if (input.self.maxMana == 0 || input.self.mana > input.self.maxMana)
		{
			return Hold("self_mana_invalid");
		}
		if (HasDuplicateHostiles(input))
		{
			return Hold("duplicate_hostile_candidate");
		}
		for (const BotMageHostileSnapshot& hostile : input.nearbyHostiles)
		{
			if (!IsValidDistance(hostile.distanceToSelf))
			{
				return Hold("hostile_distance_invalid");
			}
		}
		if (input.hasPrimaryTarget && !IsValidDistance(input.primaryTarget.distanceToSelf))
		{
			return Hold("target_distance_invalid");
		}
		if (!input.spellbookReady || !input.cooldownsReady || !input.powerReady)
		{
			return Hold("combat_state_incomplete");
		}

		const BotMageHostileSnapshot* nearestThreat = FindNearestThreat(input);
		const bool closeThreat = nearestThreat && nearestThreat->distanceToSelf <= kEmergencySpacingDistance;
		const bool pressuredDamageWindow = HasAnyTargetingSelf(input)
			|| (HasValidPrimaryTarget(input) && input.primaryTarget.distanceToSelf <= kCloseRangePressureDistance)
			|| input.self.healthPercent <= 0.60f;

		if (closeThreat)
		{
			if (capabilities.emergencySpacing.has_value())
			{
				if (!capabilities.emergencySpacing->requiresTarget && CanCastOnSelf(input, capabilities.emergencySpacing))
				{
					return CastOnSelf(BotMageDecisionType::EmergencySpacing, *capabilities.emergencySpacing, "emergency_spacing");
				}
				if (capabilities.emergencySpacing->requiresTarget && CanCastAtHostile(input, capabilities.emergencySpacing, *nearestThreat))
				{
					return CastAtHostile(BotMageDecisionType::EmergencySpacing, *capabilities.emergencySpacing, *nearestThreat, "emergency_spacing");
				}
			}
			if (input.self.healthPercent <= kSelfPreservationThreshold
				&& capabilities.selfBuffEscape.has_value()
				&& !AuraActive(input.self, capabilities.selfBuffEscape)
				&& CanCastOnSelf(input, capabilities.selfBuffEscape))
			{
				return CastOnSelf(BotMageDecisionType::CastSpell, *capabilities.selfBuffEscape, "self_preservation");
			}
			if (capabilities.control.has_value() && CanCastAtHostile(input, capabilities.control, *nearestThreat))
			{
				return CastAtHostile(BotMageDecisionType::EmergencySpacing, *capabilities.control, *nearestThreat, "emergency_control");
			}
			if (!capabilities.emergencySpacing.has_value())
			{
				return Hold("emergency_spacing_missing");
			}
			if (CooldownBlocked(input, capabilities.emergencySpacing))
			{
				return Hold("emergency_spacing_cooldown");
			}
			if (!CanAfford(input, capabilities.emergencySpacing))
			{
				return Hold("emergency_spacing_oom");
			}
			if (capabilities.control.has_value())
			{
				if (CooldownBlocked(input, capabilities.control))
				{
					return Hold("emergency_control_cooldown");
				}
				if (!CanAfford(input, capabilities.control))
				{
					return Hold("emergency_control_oom");
				}
			}
			return Hold("too_close_no_recovery");
		}

		if (input.self.healthPercent <= kSelfPreservationThreshold && capabilities.selfBuffEscape.has_value() && !AuraActive(input.self, capabilities.selfBuffEscape))
		{
			if (CanCastOnSelf(input, capabilities.selfBuffEscape))
			{
				return CastOnSelf(BotMageDecisionType::CastSpell, *capabilities.selfBuffEscape, "self_preservation");
			}
			if (CooldownBlocked(input, capabilities.selfBuffEscape))
			{
				return Hold("self_preservation_cooldown");
			}
			return Hold("self_preservation_oom");
		}

		if (!HasValidPrimaryTarget(input))
		{
			return Hold("no_hostile_target");
		}

		if (pressuredDamageWindow)
		{
			if (capabilities.instantFallback.has_value() && CanCastAtHostile(input, capabilities.instantFallback, input.primaryTarget))
			{
				return CastAtHostile(BotMageDecisionType::CastSpell, *capabilities.instantFallback, input.primaryTarget, "instant_fallback_pressure");
			}
			if (!capabilities.instantFallback.has_value())
			{
				return Hold("instant_fallback_missing");
			}
			if (CooldownBlocked(input, capabilities.instantFallback))
			{
				return Hold("instant_fallback_cooldown");
			}
			if (!CanAfford(input, capabilities.instantFallback))
			{
				return Hold("instant_fallback_oom");
			}
			return Hold("instant_fallback_out_of_range");
		}

		if (CanCastAtHostile(input, capabilities.primaryNuke, input.primaryTarget))
		{
			return CastAtHostile(BotMageDecisionType::CastSpell, *capabilities.primaryNuke, input.primaryTarget, "primary_nuke");
		}

		if (capabilities.instantFallback.has_value() && CanCastAtHostile(input, capabilities.instantFallback, input.primaryTarget))
		{
			return CastAtHostile(BotMageDecisionType::CastSpell, *capabilities.instantFallback, input.primaryTarget, "instant_fallback");
		}

		if (capabilities.instantFallback.has_value())
		{
			if (CooldownBlocked(input, capabilities.instantFallback))
			{
				return Hold("instant_fallback_cooldown");
			}
			if (!CanAfford(input, capabilities.instantFallback))
			{
				return Hold("instant_fallback_oom");
			}
		}

		if (CooldownBlocked(input, capabilities.primaryNuke))
		{
			return Hold("primary_nuke_cooldown");
		}
		if (!CanAfford(input, capabilities.primaryNuke))
		{
			return Hold("primary_nuke_oom");
		}
		if (input.primaryTarget.distanceToSelf + 0.001f < capabilities.primaryNuke->minRange)
		{
			return Hold("target_too_close");
		}
		return Hold("primary_nuke_out_of_range");
	}

	const char* ToString(const BotMageDecisionType type) noexcept
	{
		switch (type)
		{
		case BotMageDecisionType::CastSpell:
			return "cast_spell";
		case BotMageDecisionType::EmergencySpacing:
			return "emergency_spacing";
		case BotMageDecisionType::Hold:
		default:
			return "hold";
		}
	}
}
