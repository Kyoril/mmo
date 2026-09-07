// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_core/bot_session.h"

#include "base/typedefs.h"

#include <string>

namespace mmo
{
	/// Executes a Lua scenario script against a running BotSession.
	///
	/// The script runs on the main thread; every blocking API call (WaitUntil, Sleep,
	/// MoveTo, GM.CreateMonster) pumps the session's io service in a deadline loop, so
	/// no additional threads or locks are involved.
	class ScenarioEngine final
	{
	public:
		/// Runs a single scenario script.
		/// @param session The connected, world-ready session to drive.
		/// @param scriptPath Path to the .lua scenario file.
		/// @param timeoutSeconds Watchdog: the scenario fails with Timeout if it runs longer.
		/// @param transcriptPath JSONL transcript output path (empty to disable).
		/// @return The process exit code for this scenario run.
		static bot_exit_code::Type Run(
			BotSession& session,
			const std::string& scriptPath,
			uint32 timeoutSeconds,
			const std::string& transcriptPath);
	};
}
