// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_warrior_controller.h"

namespace
{
	using namespace mmo;

	constexpr float kMeleeAutoAttackRange = 5.0f;
	constexpr float kMitigationHealthThreshold = 0.60f;
	constexpr uint32 kRageSpendThreshold = 30;

	bool HasUsableTarget(const BotWarriorDecisionInput& input)
	{
		return input.hasValidTarget && input.targetGuid != 0 && input.targetAlive && input.targetHostile;
	}

	bool CanAutoAttack(const BotWarriorDecisionInput& input)
	{
		return HasUsableTarget(input) && input.targetDistance <= kMeleeAutoAttackRange;
	}

	bool AuraActive(const BotWarriorDecisionInput& input, const std::optional<BotWarriorResolvedSpell>& spell)
	{
		return spell.has_value() && input.activeAuraSpellIds.find(spell->spellId) != input.activeAuraSpellIds.end();
	}

	bool CooldownBlocked(const BotWarriorDecisionInput& input, const std::optional<BotWarriorResolvedSpell>& spell)
	{
		return spell.has_value() && input.cooldownBlockedSpellIds.find(spell->spellId) != input.cooldownBlockedSpellIds.end();
	}

	bool CanCastTargetSpell(const BotWarriorDecisionInput& input, const std::optional<BotWarriorResolvedSpell>& spell)
	{
		return spell.has_value()
			&& HasUsableTarget(input)
			&& !CooldownBlocked(input, spell)
			&& input.rage >= spell->powerCost
			&& spell->IsInRange(input.targetDistance);
	}

	bool CanCastSelfSpell(const BotWarriorDecisionInput& input, const std::optional<BotWarriorResolvedSpell>& spell)
	{
		return spell.has_value() && !CooldownBlocked(input, spell) && input.rage >= spell->powerCost;
	}

	BotWarriorDecision Hold(std::string reason)
	{
		BotWarriorDecision decision;
		decision.type = BotWarriorDecisionType::Hold;
		decision.reason = std::move(reason);
		return decision;
	}

	BotWarriorDecision EnsureAutoAttack(const BotWarriorDecisionInput& input, std::string reason)
	{
		BotWarriorDecision decision;
		decision.type = BotWarriorDecisionType::EnsureAutoAttack;
		decision.reason = std::move(reason);
		decision.ensureAutoAttack = true;
		decision.autoAttackTargetGuid = input.targetGuid;
		return decision;
	}

	BotWarriorDecision CastAtTarget(const BotWarriorDecisionInput& input, const BotWarriorResolvedSpell& spell, std::string reason)
	{
		BotWarriorDecision decision;
		decision.type = BotWarriorDecisionType::CastSpell;
		decision.reason = std::move(reason);
		decision.spellId = spell.spellId;
		decision.castTargetGuid = input.targetGuid;
		if (CanAutoAttack(input))
		{
			decision.ensureAutoAttack = true;
			decision.autoAttackTargetGuid = input.targetGuid;
		}
		return decision;
	}

	BotWarriorDecision CastOnSelf(const BotWarriorDecisionInput& input, const BotWarriorResolvedSpell& spell, std::string reason)
	{
		BotWarriorDecision decision;
		decision.type = BotWarriorDecisionType::CastSpell;
		decision.reason = std::move(reason);
		decision.spellId = spell.spellId;
		decision.castOnSelf = true;
		if (CanAutoAttack(input))
		{
			decision.ensureAutoAttack = true;
			decision.autoAttackTargetGuid = input.targetGuid;
		}
		return decision;
	}
}

namespace mmo
{
	BotWarriorDecision BotWarriorController::Evaluate(const BotWarriorDecisionInput& input) const
	{
		if (!input.capabilities || !input.capabilities->resolved)
		{
			return Hold("capabilities_unresolved");
		}

		if (input.companionMode == BotCompanionMode::Hold || input.companionMode == BotCompanionMode::Regroup)
		{
			return Hold("companion_hold");
		}

		if (!HasUsableTarget(input))
		{
			return Hold("no_valid_target");
		}

		if (!input.spellbookReady || !input.cooldownsReady || !input.powerReady)
		{
			if (CanAutoAttack(input) && (!input.autoAttackActive || input.autoAttackTargetGuid != input.targetGuid))
			{
				return EnsureAutoAttack(input, "conservative_auto_attack");
			}

			return Hold("combat_state_incomplete");
		}

		const BotWarriorCapabilities& capabilities = *input.capabilities;

		if (!input.selfInCombat && CanCastTargetSpell(input, capabilities.gapCloser))
		{
			return CastAtTarget(input, *capabilities.gapCloser, "charge_opener");
		}

		if (input.targetTargetingSelf && input.selfHealthPercent <= kMitigationHealthThreshold && !AuraActive(input, capabilities.mitigation) && CanCastSelfSpell(input, capabilities.mitigation))
		{
			return CastOnSelf(input, *capabilities.mitigation, "mitigation_window");
		}

		if (!input.targetTargetingSelf && !AuraActive(input, capabilities.partyBuff) && CanCastSelfSpell(input, capabilities.partyBuff))
		{
			return CastOnSelf(input, *capabilities.partyBuff, "maintain_party_buff");
		}

		if (capabilities.threatBuilder.has_value() && input.rage < capabilities.threatBuilder->powerCost && CanCastSelfSpell(input, capabilities.rageBuilder))
		{
			return CastOnSelf(input, *capabilities.rageBuilder, "build_rage");
		}

		if (!AuraActive(input, capabilities.armorDebuff) && CanCastTargetSpell(input, capabilities.armorDebuff))
		{
			return CastAtTarget(input, *capabilities.armorDebuff, "apply_armor_debuff");
		}

		if (CanCastTargetSpell(input, capabilities.threatBuilder))
		{
			return CastAtTarget(input, *capabilities.threatBuilder, "build_threat");
		}

		if (capabilities.rageSpender.has_value() && input.rage >= std::max(kRageSpendThreshold, capabilities.rageSpender->powerCost) && CanCastTargetSpell(input, capabilities.rageSpender))
		{
			return CastAtTarget(input, *capabilities.rageSpender, "spend_rage");
		}

		if (CanAutoAttack(input) && (!input.autoAttackActive || input.autoAttackTargetGuid != input.targetGuid))
		{
			return EnsureAutoAttack(input, "maintain_auto_attack");
		}

		return Hold("conservative_hold");
	}

	const char* ToString(const BotWarriorDecisionType type) noexcept
	{
		switch (type)
		{
		case BotWarriorDecisionType::EnsureAutoAttack:
			return "ensure_auto_attack";
		case BotWarriorDecisionType::CastSpell:
			return "cast_spell";
		case BotWarriorDecisionType::Hold:
		default:
			return "hold";
		}
	}
}
