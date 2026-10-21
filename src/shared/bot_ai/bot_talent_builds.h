// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class Project;
	}

	/// A talent build: which paths of the class tree to fill, in which order.
	struct TalentBuild final
	{
		uint32 classId { 0 };
		std::string role;

		/// Tree columns in the order they are filled.
		std::vector<uint32> columns;

		/// Talent ids taken first, in this order, wherever they are in the tree.
		std::vector<uint32> talents;
	};

	/// The next rank to learn: a talent and its zero-based rank.
	struct TalentPick final
	{
		uint32 talentId { 0 };
		uint8 rank { 0 };
	};

	/// Talent builds by class and role, read from data/bots/talent_builds.json.
	class TalentBuildBook final
	{
	public:
		/// Reads builds from a JSON file. Returns false if the file is missing or malformed;
		/// the book is then empty and bots spend no points.
		[[nodiscard]] bool Load(const std::string& path);

		/// Adds a build directly, for tests.
		void Add(TalentBuild build) { m_builds.push_back(std::move(build)); }

		/// The build for a class and role, falling back to the class's first build.
		[[nodiscard]] const TalentBuild* Find(uint32 classId, const std::string& role) const;

		[[nodiscard]] std::size_t GetCount() const { return m_builds.size(); }

	private:
		std::vector<TalentBuild> m_builds;
	};

	/// Works out the next talent rank a build wants and the tree allows.
	///
	/// Walks the pinned talents, then the build's columns row by row, and returns the first rank
	/// that is real (not a placeholder), not learned yet, whose tier is unlocked by the points
	/// already spent in the tab and whose prerequisites are met. Learned ranks are read off the
	/// bot's spell book: each talent rank is a spell.
	///
	/// @param knowsSpell Whether the bot knows a spell.
	[[nodiscard]] std::optional<TalentPick> NextTalentPick(
		const proto::Project& project,
		const TalentBuild& build,
		const std::function<bool(uint32)>& knowsSpell);
}
