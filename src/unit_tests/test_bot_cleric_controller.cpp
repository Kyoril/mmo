// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_cleric_controller.h"

namespace mmo
{
	namespace
	{
		BotClericResolvedSpell MakeSpell(
			const BotClericCapabilityKind kind,
			const uint32 spellId,
			const uint32 cost,
			const float minRange,
			const float maxRange,
			const bool requiresTarget = true,
			const bool hasExplicitRange = true)
		{
			BotClericResolvedSpell spell;
			spell.kind = kind;
			spell.spellId = spellId;
			spell.rootSpellId = spellId;
			spell.name = "spell_" + std::to_string(spellId);
			spell.powerCost = cost;
			spell.minRange = minRange;
			spell.maxRange = maxRange;
			spell.requiresTarget = requiresTarget;
			spell.hasExplicitRange = hasExplicitRange;
			return spell;
		}

		BotClericCapabilities MakeCapabilities()
		{
			BotClericCapabilities capabilities;
			capabilities.resolved = true;
			capabilities.powerType = power_type::Mana;
			capabilities.classId = 2;
			capabilities.spellFamily = 2;
			capabilities.emergencyHeal = MakeSpell(BotClericCapabilityKind::EmergencyHeal, 201, 40, 0.0f, 30.0f);
			capabilities.efficientHeal = MakeSpell(BotClericCapabilityKind::EfficientHeal, 202, 25, 0.0f, 30.0f);
			capabilities.supportAura = MakeSpell(BotClericCapabilityKind::SupportAura, 203, 20, 0.0f, 0.0f, false, false);
			capabilities.supportBuff = MakeSpell(BotClericCapabilityKind::SupportBuff, 204, 15, 0.0f, 30.0f);
			capabilities.damageFiller = MakeSpell(BotClericCapabilityKind::DamageFiller, 205, 10, 0.0f, 30.0f);
			capabilities.spells = {
				*capabilities.emergencyHeal,
				*capabilities.efficientHeal,
				*capabilities.supportAura,
				*capabilities.supportBuff,
				*capabilities.damageFiller,
			};
			return capabilities;
		}

		BotClericUnitSnapshot MakeUnit(
			const uint64 guid,
			const BotCompanionRole role,
			const float healthPercent,
			const float distance,
			const bool aware = true)
		{
			BotClericUnitSnapshot unit;
			unit.guid = guid;
			unit.role = role;
			unit.healthPercent = healthPercent;
			unit.distanceToSelf = distance;
			unit.isAware = aware;
			unit.isAlive = true;
			return unit;
		}

		BotClericDecisionInput MakeInput(const BotClericCapabilities& capabilities)
		{
			BotClericDecisionInput input;
			input.capabilities = &capabilities;
			input.companionMode = BotCompanionMode::CombatAnchor;
			input.spellbookReady = true;
			input.cooldownsReady = true;
			input.powerReady = true;
			input.hasSelf = true;
			input.self = MakeUnit(100, BotCompanionRole::Healer, 0.95f, 0.0f);
			input.self.isAware = true;
			input.mana = 100;
			input.maxMana = 100;
			input.hasHostileTarget = true;
			input.hostileTargetGuid = 9001;
			input.hostileTargetDistance = 20.0f;
			input.hostileTargetAlive = true;
			input.hostileTargetHostile = true;
			return input;
		}
	}

	TEST_CASE("cleric controller saves self before party triage", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);
		input.self.healthPercent = 0.18f;
		input.partyMembers.push_back(MakeUnit(200, BotCompanionRole::Tank, 0.12f, 18.0f));

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::CastSpell);
		CHECK(decision.reason == "self_emergency_heal");
		CHECK(decision.spellId == capabilities.emergencyHeal->spellId);
		CHECK(decision.castOnSelf);
	}

	TEST_CASE("cleric controller triages an aware tank at the exact heal mana threshold", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);
		input.self.activeAuraSpellIds.insert(capabilities.supportAura->spellId);
		input.mana = capabilities.efficientHeal->powerCost;

		BotClericUnitSnapshot tank = MakeUnit(200, BotCompanionRole::Tank, 0.68f, 30.0f);
		tank.activeAuraSpellIds.insert(capabilities.supportBuff->spellId);
		input.partyMembers.push_back(tank);

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::CastSpell);
		CHECK(decision.reason == "tank_triage_heal");
		CHECK(decision.spellId == capabilities.efficientHeal->spellId);
		CHECK(decision.castTargetGuid == tank.guid);
	}

	TEST_CASE("cleric controller refreshes support aura before filler damage when the party is stable", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);

		BotClericUnitSnapshot tank = MakeUnit(200, BotCompanionRole::Tank, 0.95f, 12.0f);
		tank.activeAuraSpellIds.insert(capabilities.supportBuff->spellId);
		input.partyMembers.push_back(tank);

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::CastSpell);
		CHECK(decision.reason == "maintain_support_aura");
		CHECK(decision.spellId == capabilities.supportAura->spellId);
		CHECK(decision.castOnSelf);
	}

	TEST_CASE("cleric controller holds conservatively when an injured ally is out of awareness", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);
		input.self.activeAuraSpellIds.insert(capabilities.supportAura->spellId);

		BotClericUnitSnapshot tank = MakeUnit(200, BotCompanionRole::Tank, 0.20f, 18.0f, false);
		tank.activeAuraSpellIds.insert(capabilities.supportBuff->spellId);
		input.partyMembers.push_back(tank);

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::Hold);
		CHECK(decision.reason == "injured_ally_out_of_awareness");
	}

	TEST_CASE("cleric controller melees in the open world when mana is below the healing reserve", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);
		input.self.activeAuraSpellIds.insert(capabilities.supportAura->spellId);
		input.mana = capabilities.efficientHeal->powerCost - 1;
		input.inDungeon = false;

		BotClericUnitSnapshot tank = MakeUnit(200, BotCompanionRole::Tank, 0.95f, 12.0f);
		tank.activeAuraSpellIds.insert(capabilities.supportBuff->spellId);
		input.partyMembers.push_back(tank);

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::MeleeAttack);
		CHECK(decision.reason == "melee_oom_fallback");
		CHECK(decision.moveTargetGuid == input.hostileTargetGuid);
	}

	TEST_CASE("cleric controller conserves mana in a dungeon when below the healing reserve", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);
		input.self.activeAuraSpellIds.insert(capabilities.supportAura->spellId);
		input.mana = capabilities.efficientHeal->powerCost - 1;
		input.inDungeon = true;

		BotClericUnitSnapshot tank = MakeUnit(200, BotCompanionRole::Tank, 0.95f, 12.0f);
		tank.activeAuraSpellIds.insert(capabilities.supportBuff->spellId);
		input.partyMembers.push_back(tank);

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::Hold);
		CHECK(decision.reason == "conserve_mana");
	}

	TEST_CASE("cleric controller spends surplus mana on offensive filler in the open world", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);
		input.self.activeAuraSpellIds.insert(capabilities.supportAura->spellId);

		BotClericUnitSnapshot tank = MakeUnit(200, BotCompanionRole::Tank, 0.96f, 12.0f);
		tank.activeAuraSpellIds.insert(capabilities.supportBuff->spellId);
		input.partyMembers.push_back(tank);

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::CastSpell);
		CHECK(decision.reason == "offensive_filler");
		CHECK(decision.spellId == capabilities.damageFiller->spellId);
		CHECK(decision.castTargetGuid == input.hostileTargetGuid);
	}

	TEST_CASE("cleric controller keeps a larger reserve before DPSing in a dungeon", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);
		input.self.activeAuraSpellIds.insert(capabilities.supportAura->spellId);
		input.inDungeon = true;
		// 50% of max mana sits below the 60% dungeon reserve, so the cleric should conserve.
		input.mana = 50;

		BotClericUnitSnapshot tank = MakeUnit(200, BotCompanionRole::Tank, 0.96f, 12.0f);
		tank.activeAuraSpellIds.insert(capabilities.supportBuff->spellId);
		input.partyMembers.push_back(tank);

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::Hold);
		CHECK(decision.reason == "conserve_mana");
	}

	TEST_CASE("cleric controller approaches an out-of-range injured ally to heal", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);
		input.self.activeAuraSpellIds.insert(capabilities.supportAura->spellId);
		// Critically injured ally, but standing beyond the emergency heal's 30 yard range.
		input.partyMembers.push_back(MakeUnit(200, BotCompanionRole::Tank, 0.10f, 40.0f));

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::Approach);
		CHECK(decision.reason == "approach_ally_emergency");
		CHECK(decision.moveTargetGuid == 200u);
		CHECK(decision.moveDesiredRange < capabilities.emergencyHeal->maxRange);
	}

	TEST_CASE("cleric controller approaches an out-of-range hostile to resume DPS", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);
		input.self.activeAuraSpellIds.insert(capabilities.supportAura->spellId);
		// Hostile beyond the damage filler's 30 yard range, with plenty of surplus mana.
		input.hostileTargetDistance = 50.0f;

		BotClericUnitSnapshot tank = MakeUnit(200, BotCompanionRole::Tank, 0.96f, 12.0f);
		tank.activeAuraSpellIds.insert(capabilities.supportBuff->spellId);
		input.partyMembers.push_back(tank);

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::Approach);
		CHECK(decision.reason == "approach_filler_range");
		CHECK(decision.moveTargetGuid == input.hostileTargetGuid);
	}

	TEST_CASE("cleric controller rejects malformed duplicate party snapshots explicitly", "[bot-cleric][controller]")
	{
		const BotClericCapabilities capabilities = MakeCapabilities();
		BotClericDecisionInput input = MakeInput(capabilities);
		input.partyMembers.push_back(MakeUnit(200, BotCompanionRole::Tank, 0.95f, 12.0f));
		input.partyMembers.push_back(MakeUnit(200, BotCompanionRole::RangedDps, 0.70f, 10.0f));

		BotClericController controller;
		const BotClericDecision decision = controller.Evaluate(input);
		REQUIRE(decision.type == BotClericDecisionType::Hold);
		CHECK(decision.reason == "duplicate_party_member");
	}
}
