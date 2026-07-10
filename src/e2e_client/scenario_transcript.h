// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include "nlohmann/json.hpp"

#include <fstream>
#include <string>

namespace mmo
{
	/// Writes a machine-readable JSONL transcript of a scenario run. Every event is
	/// flushed immediately so a crashed or killed run still leaves evidence behind.
	class ScenarioTranscript final
	{
	public:
		/// Opens the transcript file for writing (truncates an existing one).
		/// An empty path disables transcript output.
		explicit ScenarioTranscript(const std::string& path, const std::string& scenarioName);

		~ScenarioTranscript();

		/// Writes a generic event line. Additional payload fields can be passed as json.
		void Event(const std::string& type, nlohmann::json payload = nlohmann::json::object());

		/// Convenience wrappers used by the scenario API bindings.
		void Log(const std::string& message);
		void Action(const std::string& name, nlohmann::json payload = nlohmann::json::object());
		void AssertPassed(const std::string& message);
		void AssertFailed(const std::string& message);
		void ScenarioEnd(int exitCode, const std::string& outcome);

	private:
		std::ofstream m_file;
		std::string m_scenarioName;
	};
}
