// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "bot_ai/bot_talent_builds.h"

#include "proto_data/project.h"

#include <set>

using namespace mmo;

namespace
{
	constexpr uint32 Warrior = 1;
	constexpr uint32 WarriorTab = 4;

	/// Adds a talent whose ranks are spells id * 100 + rank.
	void addTalent(proto::Project& project, const uint32 id, const uint32 column, const uint32 row,
		const int ranks, const uint32 requiredPoints, const bool placeholder = false,
		const uint32 prerequisite = 0, const uint32 prerequisiteRank = 0)
	{
		proto::TalentEntry* talent = project.talents.add(id);
		talent->set_tab(WarriorTab);
		talent->set_column(column);
		talent->set_row(row);
		talent->set_required_points(requiredPoints);
		talent->set_placeholder(placeholder);
		for (int rank = 0; rank < ranks; ++rank)
		{
			talent->add_ranks(id * 100 + rank);
		}

		if (prerequisite != 0)
		{
			auto* required = talent->add_prerequisites();
			required->set_talent_id(prerequisite);
			required->set_rank(prerequisiteRank);
		}
	}

	struct Tree
	{
		Tree()
		{
			proto::TalentTabEntry* tab = project.talentTabs.add(WarriorTab);
			tab->set_name("Warrior");
			tab->set_class_id(Warrior);

			// Column 1: two first-row talents, one second-row talent behind the first, a
			// placeholder, and a capstone at 3 points.
			addTalent(project, 10, 1, 0, 2, 0);
			addTalent(project, 11, 1, 0, 1, 0);
			addTalent(project, 12, 1, 1, 1, 2, false, 10, 2);
			addTalent(project, 13, 1, 2, 1, 2, true);
			addTalent(project, 14, 1, 3, 1, 3, false, 12, 1);

			// Column 0: one talent.
			addTalent(project, 20, 0, 0, 1, 0);
		}

		std::optional<TalentPick> Next(const TalentBuild& build) const
		{
			return NextTalentPick(project, build, [this](const uint32 spell) { return known.count(spell) != 0; });
		}

		void Learn(const TalentPick& pick)
		{
			known.insert(pick.talentId * 100 + pick.rank);
		}

		proto::Project project;
		std::set<uint32> known;
	};
}

TEST_CASE("A build fills its preferred column in tier order, skipping placeholders", "[bot_ai][talents]")
{
	Tree tree;
	TalentBuild build;
	build.classId = Warrior;
	build.role = "tank";
	build.columns = { 1, 0 };

	std::vector<std::pair<uint32, uint32>> picks;
	for (int i = 0; i < 10; ++i)
	{
		const std::optional<TalentPick> pick = tree.Next(build);
		if (!pick)
		{
			break;
		}

		picks.emplace_back(pick->talentId, pick->rank);
		tree.Learn(*pick);
	}

	// Both ranks of 10, then 11, then 12 (unlocked by 2 points and 10 at rank 2), then the
	// capstone 14 at 3 points; placeholder 13 is never taken; then column 0.
	const std::vector<std::pair<uint32, uint32>> expected = { { 10, 0 }, { 10, 1 }, { 11, 0 }, { 12, 0 }, { 14, 0 }, { 20, 0 } };
	CHECK(picks == expected);
}

TEST_CASE("Pinned talents come first when the tree allows them", "[bot_ai][talents]")
{
	Tree tree;
	TalentBuild build;
	build.classId = Warrior;
	build.columns = { 1 };
	build.talents = { 20 };

	const std::optional<TalentPick> first = tree.Next(build);
	REQUIRE(first);
	CHECK(first->talentId == 20);
}

TEST_CASE("The book falls back to a class's first build for an unknown role", "[bot_ai][talents]")
{
	TalentBuildBook book;
	TalentBuild tank;
	tank.classId = Warrior;
	tank.role = "tank";
	book.Add(tank);

	TalentBuild dps;
	dps.classId = Warrior;
	dps.role = "dps";
	book.Add(dps);

	REQUIRE(book.Find(Warrior, "dps") != nullptr);
	CHECK(book.Find(Warrior, "dps")->role == "dps");
	REQUIRE(book.Find(Warrior, "healer") != nullptr);
	CHECK(book.Find(Warrior, "healer")->role == "tank");
	CHECK(book.Find(99, "dps") == nullptr);
}
