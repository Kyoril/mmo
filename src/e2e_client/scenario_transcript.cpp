// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "scenario_transcript.h"

#include "base/clock.h"
#include "log/default_log_levels.h"

#include <filesystem>

namespace mmo
{
	ScenarioTranscript::ScenarioTranscript(const std::string& path, const std::string& scenarioName)
		: m_scenarioName(scenarioName)
	{
		if (path.empty())
		{
			return;
		}

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(path).remove_filename(), ec);

		m_file.open(path, std::ios::trunc);
		if (!m_file)
		{
			WLOG("Could not open scenario transcript file " << path << " - transcript disabled");
			return;
		}

		Event("scenario_start", { { "scenario", scenarioName } });
	}

	ScenarioTranscript::~ScenarioTranscript()
	{
		if (m_file)
		{
			m_file.close();
		}
	}

	void ScenarioTranscript::Event(const std::string& type, nlohmann::json payload)
	{
		if (!m_file)
		{
			return;
		}

		payload["type"] = type;
		payload["time_ms"] = GetAsyncTimeMs();
		m_file << payload.dump() << '\n';
		m_file.flush();
	}

	void ScenarioTranscript::Log(const std::string& message)
	{
		Event("log", { { "message", message } });
	}

	void ScenarioTranscript::Action(const std::string& name, nlohmann::json payload)
	{
		payload["action"] = name;
		Event("action", std::move(payload));
	}

	void ScenarioTranscript::AssertPassed(const std::string& message)
	{
		Event("assert_pass", { { "message", message } });
	}

	void ScenarioTranscript::AssertFailed(const std::string& message)
	{
		Event("assert_fail", { { "message", message } });
	}

	void ScenarioTranscript::ScenarioEnd(const int exitCode, const std::string& outcome)
	{
		Event("scenario_end", { { "exit_code", exitCode }, { "outcome", outcome } });
	}
}
