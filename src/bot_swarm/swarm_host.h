// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_roster.h"
#include "swarm_telemetry.h"

#include "bot_ai/bot_ai_registry.h"
#include "bot_ai/bot_brain.h"
#include "bot_ai/grind_spot_index.h"
#include "bot_core/bot_nav_service.h"
#include "bot_core/bot_session.h"

#include "base/non_copyable.h"
#include "base/signal.h"
#include "base/typedefs.h"

#include "asio/io_service.hpp"

#include <memory>
#include <string>
#include <vector>

namespace mmo
{
	struct SwarmSettings final
	{
		/// Connection details, shared by every bot. Only the account and character differ.
		BotConfig connectionTemplate;

		uint32 botCount { 10 };

		/// Delay between starting one bot's login and the next. Without it, N SRP handshakes and
		/// character enumerations arrive at the login server's single database thread at once.
		GameTime loginRampMs { 250 };

		/// Host frame length. Movement is advanced every frame; bots decide far less often.
		GameTime tickMs { 50 };

		/// How long to run. Zero runs until interrupted.
		uint32 durationSeconds { 0 };

		/// Faction template the bots belong to, used to decide which spawns are worth grinding.
		/// Zero - the default - reads it off the first bot to enter the world, which is the only
		/// answer that cannot be wrong. Set it explicitly only to override that.
		uint32 playerFactionTemplate { 0 };

		/// Levels bots are rolled between.
		uint32 minLevel { 1 };
		uint32 maxLevel { 10 };

		/// Cheat each bot to its rolled level on entering the world. Requires a GM account; turn
		/// it off to watch bots level the hard way.
		bool applyLevelRoll { true };

		/// Account names are this prefix plus the bot index, and every bot shares one password.
		/// They have to match whatever tools/bots/bots_provision.ps1 registered, so both sides take
		/// them as options rather than hardcoding the same two strings in two places.
		std::string accountPrefix { "swarm" };
		std::string accountPassword { "swarmpass" };

		std::string rosterPath;
		std::string telemetryPath;
		std::string repoRoot;
	};

	/// Runs many bots in one process against a live server stack.
	///
	/// One io service, one navigation service and one spawn index are shared by every bot; see
	/// bot_core/README.md for why the navigation service in particular has to be shared rather
	/// than merely benefits from it. Everything here is single-threaded, deliberately.
	class SwarmHost final : public NonCopyable
	{
	public:
		explicit SwarmHost(SwarmSettings settings);
		~SwarmHost();

		/// Loads or builds the roster, loads the world data and builds the spawn index.
		/// @return False if the swarm cannot start at all.
		[[nodiscard]] bool Prepare();

		/// Runs until the configured duration elapses or Stop is called.
		void Run();

		/// Asks the loop to finish after the current frame. Safe to call from a signal handler.
		void Stop() { m_stopRequested = true; }

		[[nodiscard]] const SwarmCounters& GetCounters() const { return m_telemetry.GetCounters(); }

	private:
		/// One bot: its network session, its brain, and the bookkeeping the host needs.
		struct Bot final
		{
			BotRosterEntry roster;
			std::unique_ptr<BotSession> session;
			std::unique_ptr<BotBrain> brain;
			std::vector<scoped_connection> connections;

			bool loginStarted { false };
			bool failureReported { false };

			/// When to try bringing this bot back, and how many times we already have. A population
			/// meant to run for days cannot lose a bot permanently to one dropped connection.
			/// When this bot must have reached the world by, or be considered failed. Zero once it
			/// is in. A login that neither succeeds nor fails leaves a session that is not in the
			/// world and not stopped either, which nothing else here would ever notice.
			GameTime worldEntryDeadlineMs { 0 };

			GameTime reconnectAtMs { 0 };
			uint32 reconnectAttempts { 0 };
			bool wasInWorld { false };
			bool levelApplied { false };
			uint32 lastKnownLevel { 0 };

			/// Position and time the stuck watchdog last saw progress at.
			Vector3 watchdogPosition { Vector3::Zero };
			GameTime watchdogSinceMs { 0 };
		};

		void CreateBots();
		void WireBot(Bot& bot);
		void StartPendingLogins(GameTime nowMs);
		void UpdateBot(Bot& bot, GameTime nowMs);
		void OnEnteredWorld(Bot& bot);

		/// Builds the spawn index the first time a bot is in the world to say what faction it is.
		void EnsureGrindSpots(const Bot& bot);
		void CheckWatchdog(Bot& bot, GameTime nowMs);

		/// Arms the next reconnect attempt, backing off further with each one.
		void ScheduleReconnect(Bot& bot, GameTime nowMs);

		/// Brings a bot back once its scheduled time arrives.
		void ServiceReconnect(Bot& bot, GameTime nowMs);

	private:
		SwarmSettings m_settings;
		asio::io_service m_io;
		std::shared_ptr<BotNavService> m_navService;
		GrindSpotIndex m_grindSpots;
		BotAiRegistry m_registry;
		SwarmTelemetry m_telemetry;
		std::vector<BotRosterEntry> m_roster;
		std::vector<std::unique_ptr<Bot>> m_bots;
		bool m_grindSpotsBuilt { false };
		GameTime m_nextLoginMs { 0 };
		std::size_t m_nextLoginIndex { 0 };
		bool m_stopRequested { false };
	};
}
