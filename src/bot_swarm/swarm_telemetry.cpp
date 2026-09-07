// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "swarm_telemetry.h"

#include "base/clock.h"
#include "log/default_log_levels.h"

#include <utility>

namespace mmo
{
	SwarmTelemetry::SwarmTelemetry(const std::string& path)
	{
		if (path.empty())
		{
			return;
		}

		m_file.open(path, std::ios::out | std::ios::trunc);
		if (!m_file.is_open())
		{
			ELOG("Failed to open swarm telemetry file " << path);
			return;
		}

		ILOG("Swarm telemetry: " << path);
	}

	SwarmTelemetry::~SwarmTelemetry()
	{
		if (m_file.is_open())
		{
			m_file.flush();
			m_file.close();
		}
	}

	void SwarmTelemetry::Event(const uint32 botIndex, const std::string& type, nlohmann::json payload)
	{
		if (!m_file.is_open())
		{
			return;
		}

		payload["t"] = GetAsyncTimeMs();
		payload["bot"] = botIndex;
		payload["type"] = type;

		m_file << payload.dump() << "\n";
		m_file.flush();
	}

	nlohmann::json SwarmTelemetry::CountersToJson(const SwarmCounters& counters)
	{
		nlohmann::json json;
		json["bots_configured"] = counters.botsConfigured;
		json["logins_attempted"] = counters.loginsAttempted;
		json["entered_world"] = counters.enteredWorld;
		json["peak_online"] = counters.peakOnline;
		json["kills"] = counters.kills;
		json["deaths"] = counters.deaths;
		json["level_ups"] = counters.levelUps;
		json["stuck_events"] = counters.stuckEvents;
		json["disconnects"] = counters.disconnects;
		json["errors"] = counters.errors;
		json["swings"] = counters.swings;
		json["damage_dealt"] = counters.damageDealt;
		json["swing_errors"] = counters.swingErrors;
		json["path_failures"] = counters.pathFailuresByReason;
		json["actions"] = counters.actionCounts;
		return json;
	}

	void SwarmTelemetry::WriteSummary()
	{
		Event(0, "summary", CountersToJson(m_counters));
	}
}
