// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_mage_controller.h"

namespace mmo
{
	namespace
	{
		BotMageResolvedSpell MakeSpell(
			const BotMageCapabilityKind kind,
			const uint32 spellId,
			const uint32 cost,
			const float minRange,
			const float maxRange,
			const bool requiresTarget = true,
			const bool hasExplicitRange = true,
			const GameTime castTimeMs = 2500,
			const GameTime cooldownMs = 0)
		{
			BotMageResolvedSpell spell;
			spell.kind = kind;
			spell.spellId = spellId;
			spell.rootSpellId = spellId;
			spell.name = "spell_" + std::to_string(spellId);
			spell.powerCost = cost;
			spell.minRange = minRange;
			spell.maxRange = maxRange;
			spell.requiresTarget = requiresTarget;
			spell.hasExplicitRange = hasExplicitRange;
			spell.castTimeMs = castTimeMs;
			spell.cooldownMs = cooldownMs;
			return spell;
		}

		BotMageCapabilities MakeCapabilities()
		{
			BotMageCapabilities capabilities;
			capabilities.resolved = true;
			capabilities.powerType = power_type::Mana;
			capabilities.classId = 3;
			capabilities.spellFamily = 3;
			capabilities.primaryNuke = MakeSpell(BotMageCapabilityKind::PrimaryNuke, 301, 30, 8.0f, 30.0f);
			capabilities.instantFallback = MakeSpell(BotMageCapabilityKind::InstantFallback, 302, 20, 0.0f, 20.0f, true, true, 0, 8000);
			capabilities.control = MakeSpell(BotMageCapabilityKind::Control, 303, 18, 0.0f, 20.0f);
			capabilities.selfBuffEscape = MakeSpell(BotMageCapabilityKind::SelfBuffEscape, 304, 16, 0.0f, 0.0f, false, false, 0, 0);
			capabilities.emergencySpacing = MakeSpell(BotMageCapabilityKind::EmergencySpacing, 305, 25, 0.0f, 0.0f, false, false, 0, 25000);
			capabilities.spells = {
				*capabilities.primaryNuke,
				*capabilities.instantFallback,
				*capabilities.control,
				*capabilities.selfBuffEscape,
				*capabilities.emergencySpacing,
			};
			return capabilities;
		}

		BotMageHostileSnapshot MakeHostile(
			const uint64 guid,
			const float distance,
			const bool targetingSelf = false,
			const bool aware = true)
		{
			BotMageHostileSnapshot hostile;
			hostile.guid = guid;
			hostile.distanceToSelf = distance;
			hostile.targetingSelf = targetingSelf;
			hostile.isAware = aware;
			hostile.isAlive = true;
			hostile.isHostile = true;
			return hostile;
		}

		BotMageDecisionInput MakeInput(const BotMageCapabilities& capabilities)
		{
			BotMageDecisionInput input;
			input.capabilities = &capabilities;
			input.companionMode = BotCompanionMode::CombatAnchor;
			input.spellbookReady = true;
			input.cooldownsReady = true;
			input.powerReady = true;
			input.hasSelf = true;
			input.self.guid = 100;
			input.self.isAlive = true;
			input.self.healthPercent = 0.95f;
			input.self.mana = 100;
			input.self.maxMana = 100;
			input.hasPrimaryTarget = true;
			input.primaryTarget = MakeHostile(9001, 24.0f, false);
			return input;
		}
	}

	TEST_CASE("mage controller uses the primary nuke at the exact mana threshold when the target is safely ranged", "[bot-mage][controller]")
	{
		const BotMageCapabilities capabilities = MakeCapabilities();
		BotMageDecisionInput input = MakeInput(capabilities);
		input.self.mana = capabilities.primaryNuke->powerCost;
		input.self.maxMana = capabilities.primaryNuke->powerCost;

		BotMageController controller;
		const BotMageDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotMageDecisionType::CastSpell);
		CHECK(decision.reason == "primary_nuke");
		CHECK(decision.spellId == capabilities.primaryNuke->spellId);
		CHECK(decision.castTargetGuid == input.primaryTarget.guid);
	}

	TEST_CASE("mage controller prefers an instant fallback when pressure makes a long cast unsafe", "[bot-mage][controller]")
	{
		const BotMageCapabilities capabilities = MakeCapabilities();
		BotMageDecisionInput input = MakeInput(capabilities);
		input.primaryTarget.distanceToSelf = 10.0f;
		input.primaryTarget.targetingSelf = true;

		BotMageController controller;
		const BotMageDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotMageDecisionType::CastSpell);
		CHECK(decision.reason == "instant_fallback_pressure");
		CHECK(decision.spellId == capabilities.instantFallback->spellId);
		CHECK(decision.castTargetGuid == input.primaryTarget.guid);
	}

	TEST_CASE("mage controller uses explicit emergency spacing when enemies collapse the ranged band", "[bot-mage][controller]")
	{
		const BotMageCapabilities capabilities = MakeCapabilities();
		BotMageDecisionInput input = MakeInput(capabilities);
		input.nearbyHostiles.push_back(MakeHostile(9002, 4.0f, true));

		BotMageController controller;
		const BotMageDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotMageDecisionType::EmergencySpacing);
		CHECK(decision.reason == "emergency_spacing");
		CHECK(decision.spellId == capabilities.emergencySpacing->spellId);
		CHECK(decision.castOnSelf);
	}

	TEST_CASE("mage controller holds conservatively when anchor state or combat state says not to commit", "[bot-mage][controller]")
	{
		const BotMageCapabilities capabilities = MakeCapabilities();
		BotMageDecisionInput input = MakeInput(capabilities);

		BotMageController controller;
		input.companionMode = BotCompanionMode::Hold;
		const BotMageDecision holdForAnchor = controller.Evaluate(input);
		REQUIRE(holdForAnchor.type == BotMageDecisionType::Hold);
		CHECK(holdForAnchor.reason == "companion_hold");

		input.companionMode = BotCompanionMode::CombatAnchor;
		input.cooldownsReady = false;
		const BotMageDecision holdForRuntimeState = controller.Evaluate(input);
		REQUIRE(holdForRuntimeState.type == BotMageDecisionType::Hold);
		CHECK(holdForRuntimeState.reason == "combat_state_incomplete");
	}

	TEST_CASE("mage controller returns explicit conservative reasons for malformed combat inputs", "[bot-mage][controller]")
	{
		BotMageCapabilities capabilities = MakeCapabilities();
		BotMageDecisionInput input = MakeInput(capabilities);
		BotMageController controller;

		SECTION("missing self snapshot")
		{
			input.hasSelf = false;
			const BotMageDecision decision = controller.Evaluate(input);
			REQUIRE(decision.type == BotMageDecisionType::Hold);
			CHECK(decision.reason == "self_missing");
		}

		SECTION("invalid mana values")
		{
			input.self.mana = 101;
			input.self.maxMana = 100;
			const BotMageDecision decision = controller.Evaluate(input);
			REQUIRE(decision.type == BotMageDecisionType::Hold);
			CHECK(decision.reason == "self_mana_invalid");
		}

		SECTION("duplicate hostile candidates")
		{
			input.nearbyHostiles.push_back(MakeHostile(9002, 14.0f));
			input.nearbyHostiles.push_back(MakeHostile(9002, 10.0f, true));
			const BotMageDecision decision = controller.Evaluate(input);
			REQUIRE(decision.type == BotMageDecisionType::Hold);
			CHECK(decision.reason == "duplicate_hostile_candidate");
		}

		SECTION("invalid target distance")
		{
			input.primaryTarget.distanceToSelf = -1.0f;
			const BotMageDecision decision = controller.Evaluate(input);
			REQUIRE(decision.type == BotMageDecisionType::Hold);
			CHECK(decision.reason == "target_distance_invalid");
		}

		SECTION("missing emergency spacing capability")
		{
			input.nearbyHostiles.push_back(MakeHostile(9002, 4.0f, true));
			capabilities.emergencySpacing.reset();
			const BotMageDecision decision = controller.Evaluate(input);
			REQUIRE(decision.type == BotMageDecisionType::Hold);
			CHECK(decision.reason == "emergency_spacing_missing");
		}
	}

	TEST_CASE("mage controller reports explicit damage gating when range or cooldowns block safe casts", "[bot-mage][controller]")
	{
		const BotMageCapabilities capabilities = MakeCapabilities();
		BotMageController controller;

		SECTION("target beyond the primary nuke range")
		{
			BotMageDecisionInput input = MakeInput(capabilities);
			input.primaryTarget.distanceToSelf = 40.0f;
			const BotMageDecision decision = controller.Evaluate(input);
			REQUIRE(decision.type == BotMageDecisionType::Hold);
			CHECK(decision.reason == "primary_nuke_out_of_range");
		}

		SECTION("primary nuke on cooldown without a safe fallback")
		{
			BotMageDecisionInput input = MakeInput(capabilities);
			input.cooldownBlockedSpellIds.insert(capabilities.primaryNuke->spellId);
			const BotMageDecision decision = controller.Evaluate(input);
			REQUIRE(decision.type == BotMageDecisionType::Hold);
			CHECK(decision.reason == "primary_nuke_cooldown");
		}
	}
}
