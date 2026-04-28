// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_nav_service.h"
#include "mmo_bot/bot_cleric_capabilities.h"

#include <algorithm>
#include <set>

namespace mmo
{
	namespace
	{
		const proto::ClassEntry* FindClericClass(const proto::Project& project)
		{
			const auto& classes = project.classes.getTemplates();
			for (int i = 0; i < classes.entry_size(); ++i)
			{
				const auto& entry = classes.entry(i);
				if (entry.name() == "Cleric" || entry.internalname() == "cleric")
				{
					return &entry;
				}
			}

			return nullptr;
		}

		bool HasIssueContaining(const std::vector<std::string>& issues, const std::string& fragment)
		{
			return std::any_of(issues.begin(), issues.end(), [&](const std::string& issue)
			{
				return issue.find(fragment) != std::string::npos;
			});
		}

		proto::ClassEntry* AddSyntheticCleric(proto::Project& project)
		{
			auto* cleric = project.classes.add(2);
			cleric->set_name("Cleric");
			cleric->set_internalname("cleric");
			cleric->set_powertype(proto::ClassEntry::MANA);
			cleric->set_spellfamily(0);
			return cleric;
		}

		proto::SpellEntry* AddSpell(proto::Project& project, const uint32 id, const std::string& name)
		{
			auto* spell = project.spells.add(id);
			spell->set_name(name);
			return spell;
		}
	}

	TEST_CASE("cleric spellbook resolver derives healer capabilities from committed project data", "[bot-cleric][spellbook]")
	{
		BotNavService navService;
		REQUIRE(navService.IsReady());
		REQUIRE(navService.GetProject() != nullptr);

		const proto::Project& project = *navService.GetProject();
		const proto::ClassEntry* cleric = FindClericClass(project);
		REQUIRE(cleric != nullptr);

		const BotClericCapabilities capabilities = ResolveClericCapabilities(project);
		REQUIRE(capabilities.resolved);
		CHECK(capabilities.classId == cleric->id());
		CHECK(capabilities.powerType == power_type::Mana);
		CHECK_FALSE(capabilities.spells.empty());
		CHECK(capabilities.GetResolvedCategoryCount() >= 3);

		REQUIRE(capabilities.emergencyHeal.has_value());
		REQUIRE(capabilities.efficientHeal.has_value());
		REQUIRE(capabilities.damageFiller.has_value());
		CHECK(capabilities.emergencyHeal->name == "Healing Light");
		CHECK(capabilities.efficientHeal->name == "Healing Light");
		CHECK(capabilities.damageFiller->name == "Smite");
		CHECK(HasIssueContaining(capabilities.issues, "positive_missing"));
		CHECK(HasIssueContaining(capabilities.issues, "target_missing"));
	}

	TEST_CASE("cleric spellbook resolver collapses duplicate ranks and maps healer categories from synthetic data", "[bot-cleric][spellbook]")
	{
		proto::Project project;
		auto* range = project.ranges.add();
		range->set_id(1);
		range->set_range(30.0f);

		auto* cleric = AddSyntheticCleric(project);
		cleric->add_spells()->set_level(1);
		cleric->mutable_spells(0)->set_spell(100);
		cleric->add_spells()->set_level(12);
		cleric->mutable_spells(1)->set_spell(101);
		cleric->add_spells()->set_level(4);
		cleric->mutable_spells(2)->set_spell(200);
		cleric->add_spells()->set_level(6);
		cleric->mutable_spells(3)->set_spell(300);
		cleric->add_spells()->set_level(8);
		cleric->mutable_spells(4)->set_spell(400);
		cleric->add_spells()->set_level(2);
		cleric->mutable_spells(5)->set_spell(500);
		cleric->add_spells()->set_level(2);
		cleric->mutable_spells(6)->set_spell(600);

		auto* healRank1 = AddSpell(project, 100, "Healing Light");
		healRank1->set_rank(1);
		healRank1->set_nextspell(101);
		healRank1->set_positive(1);
		healRank1->set_targetmap(spell_cast_target_flags::Unit);
		healRank1->set_rangetype(1);
		healRank1->set_cost(20);
		healRank1->add_effects()->set_type(spell_effects::Heal);

		auto* healRank2 = AddSpell(project, 101, "Healing Light");
		healRank2->set_rank(2);
		healRank2->set_prevspell(100);
		healRank2->set_positive(1);
		healRank2->set_targetmap(spell_cast_target_flags::Unit);
		healRank2->set_rangetype(1);
		healRank2->set_cost(32);
		healRank2->add_effects()->set_type(spell_effects::Heal);

		auto* smite = AddSpell(project, 200, "Smite");
		smite->set_targetmap(spell_cast_target_flags::Unit);
		smite->set_rangetype(1);
		smite->set_cost(18);
		smite->add_effects()->set_type(spell_effects::SchoolDamage);
		smite->set_description("Smite an enemy for holy damage.");

		auto* aura = AddSpell(project, 300, "Healing Aura");
		aura->set_positive(1);
		aura->set_description("Increases the received healing of group members of the Cleric.");
		aura->add_effects()->set_type(spell_effects::ApplyAreaAura);

		auto* shield = AddSpell(project, 400, "Shield of Faith");
		shield->set_positive(1);
		shield->set_mechanic(spell_mechanic::Shield);
		shield->set_description("Surrounds the Cleric with a divine shield, reducing physical damage taken.");
		shield->add_effects()->set_type(spell_effects::ApplyAura);

		auto* passiveOnly = AddSpell(project, 500, "Holy Focus");
		passiveOnly->add_attributes(spell_attributes::Passive);

		auto* hiddenOnly = AddSpell(project, 600, "Hidden Cleric Passive");
		hiddenOnly->add_attributes(spell_attributes::HiddenClientSide);

		const BotClericCapabilities capabilities = ResolveClericCapabilities(project);
		REQUIRE(capabilities.resolved);
		CHECK(capabilities.spells.size() == 4);

		std::set<uint32> rootIds;
		for (const BotClericResolvedSpell& spell : capabilities.spells)
		{
			CHECK(rootIds.insert(spell.rootSpellId).second);
			CHECK_FALSE(spell.passive);
			CHECK_FALSE(spell.hidden);
		}

		REQUIRE(capabilities.emergencyHeal.has_value());
		REQUIRE(capabilities.efficientHeal.has_value());
		REQUIRE(capabilities.supportAura.has_value());
		REQUIRE(capabilities.defensive.has_value());
		REQUIRE(capabilities.damageFiller.has_value());
		CHECK(capabilities.emergencyHeal->spellId == 101);
		CHECK(capabilities.efficientHeal->spellId == 101);
		CHECK(capabilities.supportAura->spellId == 300);
		CHECK(capabilities.defensive->spellId == 400);
		CHECK(capabilities.damageFiller->spellId == 200);
	}

	TEST_CASE("cleric spellbook resolver degrades conservatively on malformed or ambiguous data", "[bot-cleric][spellbook]")
	{
		SECTION("missing cleric class returns unresolved capabilities")
		{
			proto::Project project;
			const BotClericCapabilities capabilities = ResolveClericCapabilities(project);
			CHECK_FALSE(capabilities.resolved);
			CHECK(HasIssueContaining(capabilities.issues, "cleric_class_missing"));
		}

		SECTION("passive-only spell lists stay unresolved and explicit")
		{
			proto::Project project;
			auto* cleric = AddSyntheticCleric(project);
			cleric->add_spells()->set_level(1);
			cleric->mutable_spells(0)->set_spell(500);

			auto* passiveOnly = AddSpell(project, 500, "Holy Focus");
			passiveOnly->add_attributes(spell_attributes::Passive);

			const BotClericCapabilities capabilities = ResolveClericCapabilities(project);
			CHECK_FALSE(capabilities.resolved);
			CHECK(capabilities.spells.empty());
			CHECK(HasIssueContaining(capabilities.issues, "cleric_spellbook_empty"));
			CHECK(HasIssueContaining(capabilities.issues, "emergency_heal_missing"));
			CHECK(HasIssueContaining(capabilities.issues, "damage_filler_missing"));
		}

		SECTION("unknown ranges, conflicting positivity, and ambiguous rank chains are reported")
		{
			proto::Project project;
			auto* cleric = AddSyntheticCleric(project);
			cleric->add_spells()->set_level(1);
			cleric->mutable_spells(0)->set_spell(100);
			cleric->add_spells()->set_level(1);
			cleric->mutable_spells(1)->set_spell(101);
			cleric->add_spells()->set_level(4);
			cleric->mutable_spells(2)->set_spell(200);
			cleric->add_spells()->set_level(8);
			cleric->mutable_spells(3)->set_spell(300);

			auto* ambiguousHealA = AddSpell(project, 100, "Healing Light Rank A");
			ambiguousHealA->set_baseid(100);
			ambiguousHealA->set_rank(2);
			ambiguousHealA->set_positive(1);
			ambiguousHealA->set_targetmap(spell_cast_target_flags::Unit);
			ambiguousHealA->set_rangetype(999);
			ambiguousHealA->add_effects()->set_type(spell_effects::Heal);

			auto* ambiguousHealB = AddSpell(project, 101, "Healing Light Rank B");
			ambiguousHealB->set_baseid(100);
			ambiguousHealB->set_rank(2);
			ambiguousHealB->set_positive(1);
			ambiguousHealB->set_targetmap(spell_cast_target_flags::Unit);
			ambiguousHealB->set_rangetype(999);
			ambiguousHealB->add_effects()->set_type(spell_effects::Heal);

			auto* conflictingDamage = AddSpell(project, 200, "Smite");
			conflictingDamage->set_positive(1);
			conflictingDamage->set_targetmap(spell_cast_target_flags::Unit);
			conflictingDamage->set_rangetype(999);
			conflictingDamage->add_effects()->set_type(spell_effects::SchoolDamage);

			cleric->add_spells()->set_level(10);
			cleric->mutable_spells(4)->set_spell(9999);

			auto* aura = AddSpell(project, 300, "Healing Aura");
			aura->set_positive(1);
			aura->add_effects()->set_type(spell_effects::ApplyAreaAura);

			const BotClericCapabilities capabilities = ResolveClericCapabilities(project);
			CHECK_FALSE(capabilities.resolved);
			CHECK(HasIssueContaining(capabilities.issues, "range_missing:100"));
			CHECK(HasIssueContaining(capabilities.issues, "range_missing:101"));
			CHECK(HasIssueContaining(capabilities.issues, "positive_conflict:200"));
			CHECK(HasIssueContaining(capabilities.issues, "spell_missing:9999"));
			CHECK(HasIssueContaining(capabilities.issues, "rank_chain_ambiguous:100"));
			CHECK(HasIssueContaining(capabilities.issues, "emergency_heal_missing"));
			CHECK(HasIssueContaining(capabilities.issues, "damage_filler_missing"));
		}
	}
}
