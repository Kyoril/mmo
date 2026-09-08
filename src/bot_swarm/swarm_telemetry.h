// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "nlohmann/json.hpp"

#include <fstream>
#include <map>
#include <string>

namespace mmo
{
	/// Counters a swarm run is judged on. Kept alongside the event stream rather than derived
	/// from it, so a run that is killed mid-flight still has a usable summary.
	struct SwarmCounters final
	{
		uint32 botsConfigured { 0 };
		uint32 loginsAttempted { 0 };
		uint32 enteredWorld { 0 };
		uint32 peakOnline { 0 };
		uint32 kills { 0 };
		uint32 deaths { 0 };
		uint32 levelUps { 0 };
		uint32 stuckEvents { 0 };
		uint32 disconnects { 0 };

		/// Times a bot was handed from one world node to another. A transfer takes the bot out of
		/// the world for a moment, which is indistinguishable from a disconnect unless it is
		/// counted separately - and dying triggers one whenever the bind point is on another map,
		/// so without this a bot that dies and comes back looks exactly like a bot that logged out.
		uint32 worldTransfers { 0 };

		/// Times a bot was brought back after losing its connection. Counted separately from
		/// disconnects: a run where every drop was recovered is a different story from one where
		/// the population quietly drained away, and the disconnect count alone cannot tell them
		/// apart.
		uint32 reconnects { 0 };
		uint32 errors { 0 };

		/// Landed swings and the damage they did. A swarm that swings a great deal and deals no
		/// damage is a different problem from one that never gets into melee at all, and the
		/// action counts alone cannot tell the two apart.
		uint32 swings { 0 };
		uint64 damageDealt { 0 };

		/// Swings the server refused, by reason code - out of range, facing the wrong way, and so
		/// on. The fastest route from "nothing is dying" to why.
		std::map<uint32, uint32> swingErrors;

		/// Path failures by the reason the navigation service gave, so that "the bots are stuck"
		/// can be told apart from "this spawn has no mesh under it".
		std::map<std::string, uint32> pathFailuresByReason;

		/// Actions executed, by name. The cheapest possible answer to what the swarm spent its
		/// time doing, and the first thing to look at when a run produces no kills.
		std::map<std::string, uint32> actionCounts;
	};

	/// Writes a JSONL behaviour trace of a swarm run, one line per event.
	///
	/// The line shape matches the e2e scenario transcripts on purpose: the tooling that reads
	/// those already exists, and a swarm run is the same kind of evidence at a different scale.
	/// Every line is flushed, because the interesting runs are the ones that end badly.
	class SwarmTelemetry final : public NonCopyable
	{
	public:
		/// @param path JSONL output path. Empty disables file output; counters still accumulate.
		explicit SwarmTelemetry(const std::string& path);
		~SwarmTelemetry();

		/// Writes one event. `bot` is the swarm index, so a line can always be traced back to a
		/// single bot without correlating timestamps.
		void Event(uint32 botIndex, const std::string& type, nlohmann::json payload = nlohmann::json::object());

		[[nodiscard]] SwarmCounters& GetCounters() { return m_counters; }
		[[nodiscard]] const SwarmCounters& GetCounters() const { return m_counters; }

		/// Writes the final summary line and returns the counters.
		void WriteSummary();

		[[nodiscard]] static nlohmann::json CountersToJson(const SwarmCounters& counters);

	private:
		std::ofstream m_file;
		SwarmCounters m_counters;
	};
}
