// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_mage_capabilities.h"
#include "mmo_bot/bot_nav_service.h"
#include "game/aura.h"

#include <algorithm>
#include <set>

namespace mmo
{
	namespace
	{
		const proto::ClassEntry* FindMageClass(const proto::Project& project)
		{
			const auto& classes = project.classes.getTemplates();
			for (int i = 0; i < classes.entry_size(); ++i)
			{
				const auto& entry = classes.entry(i);
				if (entry.name() == "Mage" || entry.internalname() == "mage")
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

		proto::ClassEntry* AddSyntheticMage(proto::Project& project)
		{
			auto* mage = project.classes.add(3);
			mage->set_name("Mage");
			mage->set_internalname("mage");
			mage->set_powertype(proto::ClassEntry::MANA);
			mage->set_spellfamily(1337);
			return mage;
		}

		proto::SpellEntry* AddSpell(proto::Project& project, const uint32 id, const std::string& name)
		{
			auto* spell = project.spells.add(id);
			spell->set_name(name);
			return spell;
		}
	}

	TEST_CASE("mage spellbook resolver derives ranged capabilities from committed project data", "[bot-mage][spellbook]")
	{
		BotNavService navService;
		REQUIRE(navService.IsReady());
		REQUIRE(navService.GetProject() != nullptr);

		const proto::Project& project = *navService.GetProject();
		const proto::ClassEntry* mage = FindMageClass(project);
		REQUIRE(mage != nullptr);

		const BotMageCapabilities capabilities = ResolveMageCapabilities(project);
		CHECK(capabilities.classId == mage->id());
		CHECK(capabilities.powerType == power_type::Mana);
		CHECK(capabilities.spellFamily == mage->spellfamily());
		CHECK_FALSE(capabilities.spells.empty());
		CHECK(capabilities.GetResolvedCategoryCount() >= 2);
		REQUIRE(capabilities.primaryNuke.has_value());
		CHECK(capabilities.primaryNuke->name == "Frostbolt");
		if (capabilities.emergencySpacing.has_value())
		{
			CHECK(capabilities.emergencySpacing->name == "Frost Nova");
		}
		else
		{
			CHECK(HasIssueContaining(capabilities.issues, "emergency_spacing_missing"));
			CHECK_FALSE(capabilities.resolved);
		}
		if (capabilities.instantFallback.has_value())
		{
			CHECK(capabilities.instantFallback->name == "Fire Blast");
		}
		else
		{
			CHECK(HasIssueContaining(capabilities.issues, "instant_fallback_missing"));
		}
		const bool hasControlOrEscape = capabilities.control.has_value() || capabilities.selfBuffEscape.has_value();
		CHECK(hasControlOrEscape);
	}

	TEST_CASE("mage spellbook resolver collapses duplicate ranks and maps compact mage categories from synthetic data", "[bot-mage][spellbook]")
	{
		proto::Project project;
		auto* range = project.ranges.add();
		range->set_id(1);
		range->set_range(30.0f);

		auto* mage = AddSyntheticMage(project);
		mage->add_spells()->set_level(1);
		mage->mutable_spells(0)->set_spell(100);
		mage->add_spells()->set_level(12);
		mage->mutable_spells(1)->set_spell(101);
		mage->add_spells()->set_level(8);
		mage->mutable_spells(2)->set_spell(200);
		mage->add_spells()->set_level(10);
		mage->mutable_spells(3)->set_spell(300);
		mage->add_spells()->set_level(6);
		mage->mutable_spells(4)->set_spell(400);
		mage->add_spells()->set_level(4);
		mage->mutable_spells(5)->set_spell(500);
		mage->add_spells()->set_level(3);
		mage->mutable_spells(6)->set_spell(600);
		mage->add_spells()->set_level(2);
		mage->mutable_spells(7)->set_spell(700);

		auto* frostboltRank1 = AddSpell(project, 100, "Frostbolt");
		frostboltRank1->set_rank(1);
		frostboltRank1->set_nextspell(101);
		frostboltRank1->set_positive(0);
		frostboltRank1->set_targetmap(spell_cast_target_flags::Unit);
		frostboltRank1->set_rangetype(1);
		frostboltRank1->set_casttime(3000);
		frostboltRank1->set_cost(20);
		frostboltRank1->set_spellschool(spell_school::Frost);
		frostboltRank1->add_effects()->set_type(spell_effects::SchoolDamage);
		auto* frostSlow = frostboltRank1->add_effects();
		frostSlow->set_type(spell_effects::ApplyAura);
		frostSlow->set_aura(aura_type::ModDecreaseSpeed);

		auto* frostboltRank2 = AddSpell(project, 101, "Frostbolt");
		frostboltRank2->set_rank(2);
		frostboltRank2->set_prevspell(100);
		frostboltRank2->set_positive(0);
		frostboltRank2->set_targetmap(spell_cast_target_flags::Unit);
		frostboltRank2->set_rangetype(1);
		frostboltRank2->set_casttime(2500);
		frostboltRank2->set_cost(28);
		frostboltRank2->set_spellschool(spell_school::Frost);
		frostboltRank2->add_effects()->set_type(spell_effects::SchoolDamage);
		auto* frostSlow2 = frostboltRank2->add_effects();
		frostSlow2->set_type(spell_effects::ApplyAura);
		frostSlow2->set_aura(aura_type::ModDecreaseSpeed);

		auto* fireBlast = AddSpell(project, 200, "Fire Blast");
		fireBlast->set_positive(0);
		fireBlast->set_targetmap(spell_cast_target_flags::Unit);
		fireBlast->set_rangetype(1);
		fireBlast->set_casttime(0);
		fireBlast->set_cooldown(8000);
		fireBlast->set_cost(24);
		fireBlast->set_spellschool(spell_school::Fire);
		fireBlast->set_description("Blasts a burst of fire on the enemy, dealing damage instantly.");
		fireBlast->add_effects()->set_type(spell_effects::SchoolDamage);

		auto* frostNova = AddSpell(project, 300, "Frost Nova");
		frostNova->set_positive(0);
		frostNova->set_spellschool(spell_school::Frost);
		frostNova->set_description("Releases a burst of frost magic around the caster, instantly freezing nearby enemies in place.");
		frostNova->set_mechanic(spell_mechanic::Freeze);
		frostNova->add_effects()->set_type(spell_effects::SchoolDamage);
		auto* novaRoot = frostNova->add_effects();
		novaRoot->set_type(spell_effects::ApplyAura);
		novaRoot->set_aura(aura_type::ModRoot);

		auto* disruption = AddSpell(project, 400, "Arcane Disruption");
		disruption->set_positive(0);
		disruption->set_targetmap(spell_cast_target_flags::Unit);
		disruption->set_rangetype(1);
		disruption->set_cost(15);
		disruption->set_description("Puts the target into a magic sleep, making it unable to move or perform any actions for a short time.");
		disruption->set_mechanic(spell_mechanic::Sleep);
		disruption->add_effects()->set_type(spell_effects::ApplyAura);

		auto* intellect = AddSpell(project, 500, "Arcane Intellect");
		intellect->set_positive(1);
		intellect->set_description("Increases intellect and spell damage.");
		intellect->add_effects()->set_type(spell_effects::ApplyAura);

		auto* armor = AddSpell(project, 600, "Frost Armor");
		armor->set_positive(1);
		armor->set_description("Surrounds the caster in frost, increasing armor and slowing attackers.");
		armor->add_effects()->set_type(spell_effects::ApplyAura);

		auto* passiveOnly = AddSpell(project, 700, "Improved Frostbolt");
		passiveOnly->add_attributes(spell_attributes::Passive);

		const BotMageCapabilities capabilities = ResolveMageCapabilities(project);
		REQUIRE(capabilities.resolved);
		CHECK(capabilities.spells.size() == 6);

		std::set<uint32> rootIds;
		for (const BotMageResolvedSpell& spell : capabilities.spells)
		{
			CHECK(rootIds.insert(spell.rootSpellId).second);
			CHECK_FALSE(spell.passive);
			CHECK_FALSE(spell.hidden);
		}

		REQUIRE(capabilities.primaryNuke.has_value());
		REQUIRE(capabilities.instantFallback.has_value());
		REQUIRE(capabilities.control.has_value());
		REQUIRE(capabilities.selfBuffEscape.has_value());
		REQUIRE(capabilities.emergencySpacing.has_value());
		CHECK(capabilities.primaryNuke->spellId == 101);
		CHECK(capabilities.instantFallback->spellId == 200);
		CHECK(capabilities.control->spellId == 400);
		CHECK(capabilities.selfBuffEscape->spellId == 600);
		CHECK(capabilities.emergencySpacing->spellId == 300);
	}

	TEST_CASE("mage spellbook resolver degrades conservatively on malformed or ambiguous data", "[bot-mage][spellbook]")
	{
		SECTION("missing mage class returns unresolved capabilities")
		{
			proto::Project project;
			const BotMageCapabilities capabilities = ResolveMageCapabilities(project);
			CHECK_FALSE(capabilities.resolved);
			CHECK(HasIssueContaining(capabilities.issues, "mage_class_missing"));
		}

		SECTION("passive-only spell lists stay unresolved and explicit")
		{
			proto::Project project;
			auto* mage = AddSyntheticMage(project);
			mage->add_spells()->set_level(1);
			mage->mutable_spells(0)->set_spell(700);

			auto* passiveOnly = AddSpell(project, 700, "Improved Frostbolt");
			passiveOnly->add_attributes(spell_attributes::Passive);

			const BotMageCapabilities capabilities = ResolveMageCapabilities(project);
			CHECK_FALSE(capabilities.resolved);
			CHECK(capabilities.spells.empty());
			CHECK(HasIssueContaining(capabilities.issues, "mage_spellbook_empty"));
			CHECK(HasIssueContaining(capabilities.issues, "primary_nuke_missing"));
			CHECK(HasIssueContaining(capabilities.issues, "emergency_spacing_missing"));
		}

		SECTION("unknown ranges, conflicting positivity, ambiguous rank chains, and missing spell refs are reported")
		{
			proto::Project project;
			auto* mage = AddSyntheticMage(project);
			mage->add_spells()->set_level(1);
			mage->mutable_spells(0)->set_spell(100);
			mage->add_spells()->set_level(1);
			mage->mutable_spells(1)->set_spell(101);
			mage->add_spells()->set_level(4);
			mage->mutable_spells(2)->set_spell(200);
			mage->add_spells()->set_level(8);
			mage->mutable_spells(3)->set_spell(300);
			mage->add_spells()->set_level(10);
			mage->mutable_spells(4)->set_spell(9999);

			auto* ambiguousA = AddSpell(project, 100, "Frostbolt Rank A");
			ambiguousA->set_baseid(100);
			ambiguousA->set_rank(2);
			ambiguousA->set_positive(0);
			ambiguousA->set_targetmap(spell_cast_target_flags::Unit);
			ambiguousA->set_rangetype(999);
			ambiguousA->set_casttime(2500);
			ambiguousA->add_effects()->set_type(spell_effects::SchoolDamage);

			auto* ambiguousB = AddSpell(project, 101, "Frostbolt Rank B");
			ambiguousB->set_baseid(100);
			ambiguousB->set_rank(2);
			ambiguousB->set_positive(0);
			ambiguousB->set_targetmap(spell_cast_target_flags::Unit);
			ambiguousB->set_rangetype(999);
			ambiguousB->set_casttime(2500);
			ambiguousB->add_effects()->set_type(spell_effects::SchoolDamage);

			auto* conflictingDamage = AddSpell(project, 200, "Fire Blast");
			conflictingDamage->set_positive(1);
			conflictingDamage->set_targetmap(spell_cast_target_flags::Unit);
			conflictingDamage->set_rangetype(999);
			conflictingDamage->add_effects()->set_type(spell_effects::SchoolDamage);

			auto* noTargetUtility = AddSpell(project, 300, "Arcane Intellect");
			noTargetUtility->set_positive(1);
			noTargetUtility->add_effects()->set_type(spell_effects::ApplyAura);

			const BotMageCapabilities capabilities = ResolveMageCapabilities(project);
			CHECK_FALSE(capabilities.resolved);
			CHECK(HasIssueContaining(capabilities.issues, "range_missing:100"));
			CHECK(HasIssueContaining(capabilities.issues, "range_missing:101"));
			CHECK(HasIssueContaining(capabilities.issues, "positive_conflict:200"));
			CHECK(HasIssueContaining(capabilities.issues, "spell_missing:9999"));
			CHECK(HasIssueContaining(capabilities.issues, "rank_chain_ambiguous:100"));
			CHECK(HasIssueContaining(capabilities.issues, "primary_nuke_missing"));
			CHECK(HasIssueContaining(capabilities.issues, "emergency_spacing_missing"));
		}
	}
}
