// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_nav_service.h"
#include "mmo_bot/bot_warrior_capabilities.h"

#include <algorithm>
#include <set>

namespace mmo
{
	namespace
	{
		const proto::ClassEntry* FindRageMeleeClass(const proto::Project& project)
		{
			const auto& classes = project.classes.getTemplates();
			const proto::ClassEntry* fallback = nullptr;
			for (int i = 0; i < classes.entry_size(); ++i)
			{
				const auto& entry = classes.entry(i);
				if (entry.powertype() != proto::ClassEntry::RAGE)
				{
					continue;
				}

				if (entry.has_mainhand_auto_attack_spell() && entry.mainhand_auto_attack_spell() != 0)
				{
					return &entry;
				}

				if (!fallback)
				{
					fallback = &entry;
				}
			}

			return fallback;
		}

		bool HasIssueContaining(const std::vector<std::string>& issues, const std::string& fragment)
		{
			return std::any_of(issues.begin(), issues.end(), [&](const std::string& issue)
			{
				return issue.find(fragment) != std::string::npos;
			});
		}
	}

	TEST_CASE("warrior spellbook resolver derives compact capabilities from committed project data", "[bot-warrior][spellbook]")
	{
		BotNavService navService;
		REQUIRE(navService.IsReady());
		REQUIRE(navService.GetProject() != nullptr);

		const proto::Project& project = *navService.GetProject();
		const proto::ClassEntry* warrior = FindRageMeleeClass(project);
		REQUIRE(warrior != nullptr);

		const BotWarriorCapabilities capabilities = ResolveWarriorCapabilities(project);
		REQUIRE(capabilities.resolved);
		CHECK(capabilities.classId == warrior->id());
		CHECK(capabilities.powerType == power_type::Rage);
		CHECK(capabilities.spellFamily == warrior->spellfamily());
		CHECK(capabilities.mainhandAutoAttackSpellId == warrior->mainhand_auto_attack_spell());
		CHECK_FALSE(capabilities.spells.empty());
		CHECK(capabilities.spells.size() >= 2);
		CHECK(capabilities.GetResolvedCategoryCount() >= 2);
		CHECK(capabilities.gapCloser.has_value());
		const bool hasCombatIntent = capabilities.rageBuilder.has_value()
			|| capabilities.threatBuilder.has_value()
			|| capabilities.rageSpender.has_value()
			|| capabilities.armorDebuff.has_value();
		CHECK(hasCombatIntent);
	}

	TEST_CASE("warrior spellbook resolver collapses duplicate rank chains and filters passive or hidden entries", "[bot-warrior][spellbook]")
	{
		BotNavService navService;
		REQUIRE(navService.IsReady());
		REQUIRE(navService.GetProject() != nullptr);

		const proto::Project& project = *navService.GetProject();
		const proto::ClassEntry* warrior = FindRageMeleeClass(project);
		REQUIRE(warrior != nullptr);

		const BotWarriorCapabilities capabilities = ResolveWarriorCapabilities(project);
		REQUIRE(capabilities.resolved);
		CHECK(capabilities.spells.size() < static_cast<size_t>(warrior->spells_size()));

		std::set<uint32> rootIds;
		for (const BotWarriorResolvedSpell& spell : capabilities.spells)
		{
			CHECK(rootIds.insert(spell.rootSpellId).second);
			CHECK_FALSE(spell.passive);
			CHECK_FALSE(spell.hidden);
			CHECK(spell.spellId != 0);
			CHECK_FALSE(spell.name.empty());
		}
	}

	TEST_CASE("warrior spellbook resolver degrades conservatively on malformed class and spell data", "[bot-warrior][spellbook]")
	{
		SECTION("missing warrior class returns unresolved capabilities")
		{
			proto::Project project;
			const BotWarriorCapabilities capabilities = ResolveWarriorCapabilities(project);
			CHECK_FALSE(capabilities.resolved);
			CHECK(HasIssueContaining(capabilities.issues, "warrior_class_missing"));
		}

		SECTION("unknown range ids and duplicate roots stay explicit without leaking passive-only entries")
		{
			proto::Project project;
			auto* warrior = project.classes.add(1);
			warrior->set_name("Synthetic Warrior");
			warrior->set_internalname("synthetic_warrior");
			warrior->set_powertype(proto::ClassEntry::RAGE);
			warrior->set_spellfamily(77);
			warrior->set_mainhand_auto_attack_spell(100);
			warrior->add_spells()->set_spell(100);
			warrior->mutable_spells(0)->set_level(1);
			warrior->add_spells()->set_spell(200);
			warrior->mutable_spells(1)->set_level(4);
			warrior->add_spells()->set_spell(201);
			warrior->mutable_spells(2)->set_level(10);
			warrior->add_spells()->set_spell(300);
			warrior->mutable_spells(3)->set_level(2);

			auto* autoAttack = project.spells.add(100);
			autoAttack->set_name("Auto Attack");
			autoAttack->set_targetmap(spell_cast_target_flags::Unit);
			autoAttack->set_maxrange(5.0f);

			auto* strikeRank1 = project.spells.add(200);
			strikeRank1->set_name("Synthetic Strike");
			strikeRank1->set_rank(1);
			strikeRank1->set_nextspell(201);
			strikeRank1->set_targetmap(spell_cast_target_flags::Unit);
			strikeRank1->set_rangetype(999);
			strikeRank1->set_cost(10);
			strikeRank1->add_effects()->set_type(spell_effects::WeaponDamage);

			auto* strikeRank2 = project.spells.add(201);
			strikeRank2->set_name("Synthetic Strike Rank 2");
			strikeRank2->set_rank(2);
			strikeRank2->set_prevspell(200);
			strikeRank2->set_targetmap(spell_cast_target_flags::Unit);
			strikeRank2->set_rangetype(999);
			strikeRank2->set_cost(15);
			strikeRank2->add_effects()->set_type(spell_effects::WeaponDamage);

			auto* passiveOnly = project.spells.add(300);
			passiveOnly->set_name("Passive Talent");
			passiveOnly->add_attributes(spell_attributes::Passive);

			const BotWarriorCapabilities capabilities = ResolveWarriorCapabilities(project);
			CHECK(capabilities.resolved);
			CHECK(capabilities.spells.size() == 2);
			REQUIRE(capabilities.threatBuilder.has_value());
			CHECK(capabilities.threatBuilder->spellId == 201);
			CHECK(HasIssueContaining(capabilities.issues, "range_missing:200"));
			CHECK(HasIssueContaining(capabilities.issues, "range_missing:201"));
		}
	}
}
