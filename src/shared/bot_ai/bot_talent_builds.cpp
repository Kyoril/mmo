// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_talent_builds.h"

#include "proto_data/project.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <fstream>

namespace mmo
{
	namespace
	{
		/// Ranks of a talent the bot has learned: the highest rank whose spell it knows, plus one.
		uint32 learnedRanks(const proto::TalentEntry& talent, const std::function<bool(uint32)>& knowsSpell)
		{
			for (int rank = talent.ranks_size(); rank > 0; --rank)
			{
				if (knowsSpell(talent.ranks(rank - 1)))
				{
					return static_cast<uint32>(rank);
				}
			}

			return 0;
		}

		const proto::TalentTabEntry* tabForClass(const proto::Project& project, const uint32 classId)
		{
			for (const proto::TalentTabEntry& tab : project.talentTabs.getTemplates().entry())
			{
				if (tab.class_id() == classId)
				{
					return &tab;
				}
			}

			return nullptr;
		}
	}

	bool TalentBuildBook::Load(const std::string& path)
	{
		m_builds.clear();

		std::ifstream file(path);
		if (!file)
		{
			return false;
		}

		const nlohmann::json data = nlohmann::json::parse(file, nullptr, false);
		if (data.is_discarded() || !data.contains("builds") || !data["builds"].is_array())
		{
			return false;
		}

		for (const nlohmann::json& entry : data["builds"])
		{
			if (!entry.is_object() || !entry.contains("class"))
			{
				continue;
			}

			TalentBuild build;
			build.classId = entry.value("class", 0u);
			build.role = entry.value("role", std::string("dps"));
			build.columns = entry.value("columns", std::vector<uint32> {});
			build.talents = entry.value("talents", std::vector<uint32> {});
			m_builds.push_back(std::move(build));
		}

		return true;
	}

	const TalentBuild* TalentBuildBook::Find(const uint32 classId, const std::string& role) const
	{
		const TalentBuild* fallback = nullptr;
		for (const TalentBuild& build : m_builds)
		{
			if (build.classId != classId)
			{
				continue;
			}

			if (build.role == role)
			{
				return &build;
			}

			fallback = fallback ? fallback : &build;
		}

		return fallback;
	}

	std::optional<TalentPick> NextTalentPick(
		const proto::Project& project,
		const TalentBuild& build,
		const std::function<bool(uint32)>& knowsSpell)
	{
		const proto::TalentTabEntry* tab = tabForClass(project, build.classId);
		if (!tab)
		{
			return std::nullopt;
		}

		std::vector<const proto::TalentEntry*> tree;
		uint32 spentInTab = 0;
		for (const proto::TalentEntry& talent : project.talents.getTemplates().entry())
		{
			if (talent.tab() == tab->id())
			{
				tree.push_back(&talent);
				spentInTab += learnedRanks(talent, knowsSpell);
			}
		}

		const auto findTalent = [&tree](const uint32 id) -> const proto::TalentEntry*
		{
			for (const proto::TalentEntry* talent : tree)
			{
				if (talent->id() == id)
				{
					return talent;
				}
			}

			return nullptr;
		};

		// The order a bot fills the tree: pinned talents first, then the columns, top row first.
		std::vector<const proto::TalentEntry*> order;
		for (const uint32 id : build.talents)
		{
			if (const proto::TalentEntry* talent = findTalent(id))
			{
				order.push_back(talent);
			}
		}

		for (const uint32 column : build.columns)
		{
			std::vector<const proto::TalentEntry*> inColumn;
			for (const proto::TalentEntry* talent : tree)
			{
				if (talent->column() == column)
				{
					inColumn.push_back(talent);
				}
			}

			std::sort(inColumn.begin(), inColumn.end(), [](const proto::TalentEntry* a, const proto::TalentEntry* b)
				{
					return a->row() != b->row() ? a->row() < b->row() : a->id() < b->id();
				});
			order.insert(order.end(), inColumn.begin(), inColumn.end());
		}

		for (const proto::TalentEntry* talent : order)
		{
			if (talent->placeholder() || talent->ranks_size() == 0)
			{
				continue;
			}

			const uint32 learned = learnedRanks(*talent, knowsSpell);
			if (learned >= static_cast<uint32>(talent->ranks_size()))
			{
				continue;
			}

			if (spentInTab < talent->required_points())
			{
				continue;
			}

			bool prerequisitesMet = true;
			for (const auto& prerequisite : talent->prerequisites())
			{
				const proto::TalentEntry* required = findTalent(prerequisite.talent_id());
				if (!required || learnedRanks(*required, knowsSpell) < prerequisite.rank())
				{
					prerequisitesMet = false;
					break;
				}
			}

			if (prerequisitesMet)
			{
				return TalentPick { talent->id(), static_cast<uint8>(learned) };
			}
		}

		return std::nullopt;
	}
}
