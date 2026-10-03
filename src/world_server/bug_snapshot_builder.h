// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/bug_report.h"

#include "nlohmann/json.hpp"

namespace mmo
{
	class Player;

	namespace proto
	{
		class Project;
	}

	/// Builds the server-side part of a bug report: authoritative state the client cannot see.
	///
	/// Runs on the world thread and reads game objects directly. The result always contains the
	/// reporting character (identity, position, health and power, auras, target, recent combat
	/// events) and, depending on the subject type, the server's view of the reported item, spell,
	/// creature, quest, aura or world object.
	nlohmann::json BuildBugReportServerSnapshot(Player& player, const game::BugReportPayload& payload, const proto::Project& project);
}
