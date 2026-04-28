// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_warrior_controller.h"

namespace mmo
{
	namespace
	{
		BotWarriorResolvedSpell MakeSpell(
			const BotWarriorCapabilityKind kind,
			const uint32 spellId,
			const uint32 cost,
			const float minRange,
			const float maxRange,
			const bool hasExplicitRange = true)
		{
			BotWarriorResolvedSpell spell;
			spell.kind = kind;
			spell.spellId = spellId;
			spell.rootSpellId = spellId;
			spell.name = "spell_" + std::to_string(spellId);
			spell.powerCost = cost;
			spell.minRange = minRange;
			spell.maxRange = maxRange;
			spell.hasExplicitRange = hasExplicitRange;
			spell.requiresTarget = kind != BotWarriorCapabilityKind::RageBuilder
				&& kind != BotWarriorCapabilityKind::PartyBuff
				&& kind != BotWarriorCapabilityKind::Mitigation;
			return spell;
		}

		BotWarriorCapabilities MakeCapabilities()
		{
			BotWarriorCapabilities capabilities;
			capabilities.resolved = true;
			capabilities.powerType = power_type::Rage;
			capabilities.classId = 1;
			capabilities.spellFamily = 1;
			capabilities.mainhandAutoAttackSpellId = 100;
			capabilities.gapCloser = MakeSpell(BotWarriorCapabilityKind::GapCloser, 101, 0, 8.0f, 25.0f);
			capabilities.threatBuilder = MakeSpell(BotWarriorCapabilityKind::ThreatBuilder, 102, 15, 0.0f, 5.0f);
			capabilities.armorDebuff = MakeSpell(BotWarriorCapabilityKind::ArmorDebuff, 103, 10, 0.0f, 5.0f);
			capabilities.rageBuilder = MakeSpell(BotWarriorCapabilityKind::RageBuilder, 104, 0, 0.0f, 0.0f, false);
			capabilities.partyBuff = MakeSpell(BotWarriorCapabilityKind::PartyBuff, 105, 10, 0.0f, 0.0f, false);
			capabilities.mitigation = MakeSpell(BotWarriorCapabilityKind::Mitigation, 106, 10, 0.0f, 0.0f, false);
			capabilities.rageSpender = MakeSpell(BotWarriorCapabilityKind::RageSpender, 107, 30, 0.0f, 5.0f);
			capabilities.spells = {
				*capabilities.gapCloser,
				*capabilities.threatBuilder,
				*capabilities.armorDebuff,
				*capabilities.rageBuilder,
				*capabilities.partyBuff,
				*capabilities.mitigation,
				*capabilities.rageSpender,
			};
			return capabilities;
		}

		BotWarriorDecisionInput MakeInput(const BotWarriorCapabilities& capabilities)
		{
			BotWarriorDecisionInput input;
			input.capabilities = &capabilities;
			input.companionMode = BotCompanionMode::CombatAnchor;
			input.spellbookReady = true;
			input.cooldownsReady = true;
			input.powerReady = true;
			input.selfInCombat = true;
			input.hasValidTarget = true;
			input.targetAlive = true;
			input.targetHostile = true;
			input.targetTargetingSelf = true;
			input.targetGuid = 9001;
			input.targetDistance = 3.0f;
			input.rage = 35;
			input.selfHealthPercent = 0.95f;
			return input;
		}
	}

	TEST_CASE("warrior controller opens with a gap closer before melee contact", "[bot-warrior][controller]")
	{
		const BotWarriorCapabilities capabilities = MakeCapabilities();
		BotWarriorDecisionInput input = MakeInput(capabilities);
		input.selfInCombat = false;
		input.targetDistance = 12.0f;
		input.rage = 0;

		BotWarriorController controller;
		const BotWarriorDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotWarriorDecisionType::CastSpell);
		CHECK(decision.reason == "charge_opener");
		CHECK(decision.spellId == capabilities.gapCloser->spellId);
		CHECK(decision.castTargetGuid == input.targetGuid);
	}

	TEST_CASE("warrior controller uses mitigation before spending rage when tank pressure spikes", "[bot-warrior][controller]")
	{
		const BotWarriorCapabilities capabilities = MakeCapabilities();
		BotWarriorDecisionInput input = MakeInput(capabilities);
		input.selfHealthPercent = 0.45f;

		BotWarriorController controller;
		const BotWarriorDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotWarriorDecisionType::CastSpell);
		CHECK(decision.reason == "mitigation_window");
		CHECK(decision.spellId == capabilities.mitigation->spellId);
		CHECK(decision.castOnSelf);
		CHECK(decision.ensureAutoAttack);
		CHECK(decision.autoAttackTargetGuid == input.targetGuid);
	}

	TEST_CASE("warrior controller refreshes its party buff before normal threat builders when safe", "[bot-warrior][controller]")
	{
		const BotWarriorCapabilities capabilities = MakeCapabilities();
		BotWarriorDecisionInput input = MakeInput(capabilities);
		input.targetTargetingSelf = false;
		input.autoAttackActive = true;
		input.autoAttackTargetGuid = input.targetGuid;

		BotWarriorController controller;
		const BotWarriorDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotWarriorDecisionType::CastSpell);
		CHECK(decision.reason == "maintain_party_buff");
		CHECK(decision.spellId == capabilities.partyBuff->spellId);
		CHECK(decision.castOnSelf);
	}

	TEST_CASE("warrior controller falls back to rage generation when exact threat thresholds are not met", "[bot-warrior][controller]")
	{
		const BotWarriorCapabilities capabilities = MakeCapabilities();
		BotWarriorDecisionInput input = MakeInput(capabilities);
		input.rage = capabilities.threatBuilder->powerCost - 1;
		input.autoAttackActive = true;
		input.autoAttackTargetGuid = input.targetGuid;
		input.activeAuraSpellIds.insert(capabilities.partyBuff->spellId);

		BotWarriorController controller;
		const BotWarriorDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotWarriorDecisionType::CastSpell);
		CHECK(decision.reason == "build_rage");
		CHECK(decision.spellId == capabilities.rageBuilder->spellId);
		CHECK(decision.castOnSelf);
	}

	TEST_CASE("warrior controller applies threat builders and keeps auto attack armed in melee", "[bot-warrior][controller]")
	{
		const BotWarriorCapabilities capabilities = MakeCapabilities();
		BotWarriorDecisionInput input = MakeInput(capabilities);
		input.activeAuraSpellIds.insert(capabilities.partyBuff->spellId);

		BotWarriorController controller;
		const BotWarriorDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotWarriorDecisionType::CastSpell);
		CHECK(decision.reason == "apply_armor_debuff");
		CHECK(decision.spellId == capabilities.armorDebuff->spellId);
		CHECK(decision.castTargetGuid == input.targetGuid);
		CHECK(decision.ensureAutoAttack);
		CHECK(decision.autoAttackTargetGuid == input.targetGuid);
	}

	TEST_CASE("warrior controller degrades to conservative auto attack when runtime state is incomplete", "[bot-warrior][controller]")
	{
		const BotWarriorCapabilities capabilities = MakeCapabilities();
		BotWarriorDecisionInput input = MakeInput(capabilities);
		input.spellbookReady = false;
		input.cooldownsReady = false;
		input.powerReady = false;
		input.activeAuraSpellIds.insert(capabilities.partyBuff->spellId);

		BotWarriorController controller;
		const BotWarriorDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotWarriorDecisionType::EnsureAutoAttack);
		CHECK(decision.reason == "conservative_auto_attack");
		CHECK(decision.autoAttackTargetGuid == input.targetGuid);
	}

	TEST_CASE("warrior controller holds explicitly when anchor or target state says not to commit", "[bot-warrior][controller]")
	{
		const BotWarriorCapabilities capabilities = MakeCapabilities();
		BotWarriorDecisionInput input = MakeInput(capabilities);
		input.companionMode = BotCompanionMode::Hold;

		BotWarriorController controller;
		const BotWarriorDecision holdForAnchor = controller.Evaluate(input);
		REQUIRE(holdForAnchor.type == BotWarriorDecisionType::Hold);
		CHECK(holdForAnchor.reason == "companion_hold");

		input.companionMode = BotCompanionMode::CombatAnchor;
		input.hasValidTarget = false;
		const BotWarriorDecision holdForTarget = controller.Evaluate(input);
		REQUIRE(holdForTarget.type == BotWarriorDecisionType::Hold);
		CHECK(holdForTarget.reason == "no_valid_target");
	}
}
