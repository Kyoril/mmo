// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "swarm_host.h"

#include "bot_ai/strategies/grind_strategy.h"
#include "bot_core/bot_movement_controller.h"
#include "bot_core/bot_realm_connector.h"
#include "bot_core/bot_unit.h"

#include "base/clock.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace mmo
{
	namespace
	{
		/// A bot that has neither moved nor got anywhere for this long is wedged in a way its own
		/// movement watchdog did not catch - most often standing next to something it decided to
		/// attack and cannot reach.
		constexpr GameTime WatchdogTimeoutMs = 60000;

		/// How far the bot has to move for the watchdog to count it as progress.
		constexpr float WatchdogProgressDistance = 5.0f;
	}

	SwarmHost::SwarmHost(SwarmSettings settings)
		: m_settings(std::move(settings))
		, m_telemetry(m_settings.telemetryPath)
	{
	}

	SwarmHost::~SwarmHost() = default;

	bool SwarmHost::Prepare()
	{
		// One navigation service for the whole process. Its constructor asserts on a second
		// instance, because AssetRegistry is a class of statics.
		m_navService = std::make_shared<BotNavService>(m_settings.repoRoot);

		const proto::Project* project = m_navService->GetProject();
		if (!project)
		{
			ELOG("Swarm cannot start: the world data failed to load");
			return false;
		}

		if (m_settings.playerFactionTemplate != 0)
		{
			m_grindSpots.Build(*project, m_settings.playerFactionTemplate);
			m_grindSpotsBuilt = true;
		}

		RegisterGrindStrategies(m_registry);

		for (const std::string& reference : m_registry.FindDanglingReferences())
		{
			ELOG("Bot strategy refers to an unregistered definition: " << reference);
		}

		if (!m_settings.rosterPath.empty() && LoadRoster(m_settings.rosterPath, m_roster))
		{
			ILOG("Loaded roster of " << m_roster.size() << " bots from " << m_settings.rosterPath);
		}
		else
		{
			m_roster.clear();
		}

		// Grow the roster to cover this run, but never shrink the stored one. A run with a smaller
		// --bots than last time is asking for fewer bots, not asking to forget the accounts the
		// others were provisioned against - and those accounts still exist on the login server.
		if (m_roster.size() < m_settings.botCount)
		{
			const std::vector<BotRosterEntry> full = BuildRoster(m_settings.botCount,
				m_settings.accountPrefix, m_settings.accountPassword,
				m_settings.minLevel, m_settings.maxLevel, 1);

			for (std::size_t index = m_roster.size(); index < full.size(); ++index)
			{
				m_roster.push_back(full[index]);
			}

			ILOG("Roster extended to " << m_roster.size() << " bots");
		}

		if (m_roster.empty())
		{
			ELOG("Swarm cannot start: the roster is empty");
			return false;
		}

		if (!m_settings.rosterPath.empty())
		{
			static_cast<void>(SaveRoster(m_settings.rosterPath, m_roster));
		}

		// Only now trim to what this run actually uses. The file keeps everyone.
		if (m_roster.size() > m_settings.botCount)
		{
			m_roster.resize(m_settings.botCount);
		}

		CreateBots();
		m_telemetry.GetCounters().botsConfigured = static_cast<uint32>(m_bots.size());
		return true;
	}

	void SwarmHost::CreateBots()
	{
		m_bots.reserve(m_roster.size());

		for (const BotRosterEntry& entry : m_roster)
		{
			auto bot = std::make_unique<Bot>();
			bot->roster = entry;

			BotConfig config = m_settings.connectionTemplate;
			config.username = entry.account;
			config.password = entry.password;
			config.characterName = entry.characterName;
			config.createCharacter = true;
			config.race = entry.race;
			config.characterClass = entry.characterClass;
			config.gender = entry.gender;

			bot->session = std::make_unique<BotSession>(m_io, m_navService, std::move(config));
			bot->brain = std::make_unique<BotBrain>(*bot->session, m_registry, entry.index);
			bot->brain->SetGrindSpots(&m_grindSpots);

			WireBot(*bot);
			m_bots.push_back(std::move(bot));
		}
	}

	void SwarmHost::WireBot(Bot& bot)
	{
		const uint32 index = bot.roster.index;

		bot.connections.emplace_back(bot.brain->ActionExecuted.connect(
			[this, index](const std::string& action, const float relevance)
			{
				++m_telemetry.GetCounters().actionCounts[action];
				m_telemetry.Event(index, "action", { { "name", action }, { "relevance", relevance } });
			}));

		bot.connections.emplace_back(bot.brain->StateChanged.connect(
			[this, index](const BotAiState from, const BotAiState to)
			{
				if (to == BotAiState::Dead)
				{
					++m_telemetry.GetCounters().deaths;
				}

				m_telemetry.Event(index, "state_change",
					{ { "from", GetBotAiStateName(from) }, { "to", GetBotAiStateName(to) } });
			}));

		bot.connections.emplace_back(bot.brain->TargetKilled.connect(
			[this, index](const uint64 guid)
			{
				++m_telemetry.GetCounters().kills;
				m_telemetry.Event(index, "kill", { { "target", guid } });
			}));

		bot.connections.emplace_back(bot.session->GetRealm().AttackHit.connect(
			[this, index](const uint64 attacker, const uint64 victim, const uint32 damage,
				const uint32 hitInfo, const uint32 victimState)
			{
				++m_telemetry.GetCounters().swings;
				m_telemetry.GetCounters().damageDealt += damage;
				m_telemetry.Event(index, "attack_hit",
					{ { "victim", victim }, { "damage", damage }, { "hit_info", hitInfo },
					  { "victim_state", victimState } });
			}));

		bot.connections.emplace_back(bot.session->GetRealm().AttackSwingError.connect(
			[this, index](const AttackSwingEvent event)
			{
				++m_telemetry.GetCounters().swingErrors[static_cast<uint32>(event)];
				m_telemetry.Event(index, "swing_error", { { "event", static_cast<uint32>(event) } });
			}));

		bot.connections.emplace_back(bot.session->GetMovementController().MovementStarted.connect(
			[this, index](const BotMovementEvent& event)
			{
				m_telemetry.Event(index, "move_start",
					{ { "x", event.target.x }, { "y", event.target.y }, { "z", event.target.z },
					  { "points", event.pathPointCount } });
			}));

		bot.connections.emplace_back(bot.session->GetMovementController().TargetReached.connect(
			[this, index](const BotMovementEvent& event)
			{
				m_telemetry.Event(index, "move_reached", { { "reason", event.reason } });
			}));

		bot.connections.emplace_back(bot.session->GetMovementController().MovementStopped.connect(
			[this, index](const BotMovementEvent& event)
			{
				m_telemetry.Event(index, "move_stop", { { "reason", event.reason } });
			}));

		bot.connections.emplace_back(bot.session->GetMovementController().TargetUnreachable.connect(
			[this, index](const BotMovementEvent& event)
			{
				++m_telemetry.GetCounters().pathFailuresByReason[event.reason];
				m_telemetry.Event(index, "path_fail", { { "reason", event.reason } });
			}));
	}

	void SwarmHost::StartPendingLogins(const GameTime nowMs)
	{
		if (m_nextLoginIndex >= m_bots.size() || nowMs < m_nextLoginMs)
		{
			return;
		}

		Bot& bot = *m_bots[m_nextLoginIndex];
		++m_nextLoginIndex;
		m_nextLoginMs = nowMs + m_settings.loginRampMs;

		bot.loginStarted = true;
		++m_telemetry.GetCounters().loginsAttempted;
		m_telemetry.Event(bot.roster.index, "login",
			{ { "account", bot.roster.account }, { "character", bot.roster.characterName } });

		if (!bot.session->Start())
		{
			++m_telemetry.GetCounters().errors;
			m_telemetry.Event(bot.roster.index, "error", { { "message", "session start failed" } });
		}
	}

	void SwarmHost::EnsureGrindSpots(const Bot& bot)
	{
		if (m_grindSpotsBuilt)
		{
			return;
		}

		// Waiting for the bot's own object, not for a faction template: this dataset leaves race
		// faction templates at zero, and treating that as "not ready yet" would wait forever.
		const BotUnit* self = bot.session->GetContext().GetSelf();
		if (!self)
		{
			return;
		}

		const proto::Project* project = m_navService->GetProject();
		if (!project)
		{
			return;
		}

		// Asking the bot rather than the operator. Which spawns are hostile depends on the
		// faction the characters actually got, and a flag defaulted to the wrong template
		// produces an empty index and a swarm that silently never fights anything.
		m_settings.playerFactionTemplate = self->GetFactionTemplate();
		m_grindSpots.Build(*project, m_settings.playerFactionTemplate);
		m_grindSpotsBuilt = true;

		if (m_grindSpots.GetSpotCount() == 0)
		{
			WLOG("No grind spots found for faction template " << m_settings.playerFactionTemplate
				<< "; bots will fight what walks into them but will not go looking");
		}
	}

	void SwarmHost::OnEnteredWorld(Bot& bot)
	{
		++m_telemetry.GetCounters().enteredWorld;
		m_telemetry.Event(bot.roster.index, "enter_world",
			{ { "character", bot.roster.characterName }, { "level", bot.roster.targetLevel } });

		bot.brain->Reset();

		if (m_settings.applyLevelRoll && !bot.levelApplied && bot.roster.targetLevel > 1)
		{
			// Levelling up front rather than starting everyone at one is what puts the whole
			// level range of the game under load at once, instead of only its first hour.
			const uint32 levels = bot.roster.targetLevel - 1;
			bot.session->GetRealm().CheatLevelUp(static_cast<uint8>(std::min<uint32>(levels, 255)));
			bot.levelApplied = true;
		}
	}

	void SwarmHost::CheckWatchdog(Bot& bot, const GameTime nowMs)
	{
		const Vector3 position = bot.session->GetContext().GetPosition();

		if (bot.watchdogSinceMs == 0
			|| (position - bot.watchdogPosition).GetLength() > WatchdogProgressDistance)
		{
			bot.watchdogPosition = position;
			bot.watchdogSinceMs = nowMs;
			return;
		}

		if (nowMs - bot.watchdogSinceMs < WatchdogTimeoutMs)
		{
			return;
		}

		// The bot's own movement watchdog only covers a path in flight. This one covers the
		// other way of being stuck: standing still, deciding, and getting nowhere.
		++m_telemetry.GetCounters().stuckEvents;
		m_telemetry.Event(bot.roster.index, "stuck",
			{ { "x", position.x }, { "y", position.y }, { "z", position.z },
			  { "last_action", bot.brain->GetLastAction() } });

		bot.brain->GetContext().GetGrindState().ClearSpot();
		bot.watchdogSinceMs = nowMs;
	}

	void SwarmHost::UpdateBot(Bot& bot, const GameTime nowMs)
	{
		if (!bot.loginStarted)
		{
			return;
		}

		bot.session->Update();

		// A session that has given up says so through its exit code and nothing else. Without this
		// a bot that never reaches the world just quietly does not appear in the count, and the
		// run reports "4 of 5 entered the world" with no way to find out why.
		if (bot.session->IsStopRequested() && !bot.failureReported)
		{
			bot.failureReported = true;
			++m_telemetry.GetCounters().errors;
			m_telemetry.Event(bot.roster.index, "error",
				{ { "message", "session stopped before entering the world" },
				  { "exit_code", static_cast<int32>(bot.session->GetExitCode()) },
				  { "account", bot.roster.account },
				  { "character", bot.roster.characterName } });
		}

		const bool inWorld = bot.session->IsWorldReady();
		if (inWorld && !bot.wasInWorld)
		{
			OnEnteredWorld(bot);
		}
		else if (!inWorld && bot.wasInWorld)
		{
			++m_telemetry.GetCounters().disconnects;

			const auto reason = bot.session->GetKickReason();
			m_telemetry.Event(bot.roster.index, "disconnect",
				{ { "kick_reason", reason.has_value() ? static_cast<int32>(*reason) : -1 } });
		}

		bot.wasInWorld = inWorld;

		if (!inWorld)
		{
			return;
		}

		// Retried every tick until it succeeds rather than done once on entering the world: at
		// the moment the world says the bot is in, its own object has not been sent yet, so there
		// is nothing to read a faction template off.
		EnsureGrindSpots(bot);

		const BotUnit* self = bot.session->GetContext().GetSelf();
		const uint32 level = self ? self->GetLevel() : 0;

		if (level > bot.lastKnownLevel && bot.lastKnownLevel != 0)
		{
			++m_telemetry.GetCounters().levelUps;
			m_telemetry.Event(bot.roster.index, "level_up", { { "level", level } });
		}

		bot.lastKnownLevel = level;

		bot.brain->Update(nowMs);
		CheckWatchdog(bot, nowMs);
	}

	void SwarmHost::Run()
	{
		const GameTime startMs = GetAsyncTimeMs();
		const GameTime endMs = m_settings.durationSeconds > 0
			? startMs + static_cast<GameTime>(m_settings.durationSeconds) * 1000
			: 0;

		ILOG("Swarm running: " << m_bots.size() << " bots, tick " << m_settings.tickMs << "ms");

		while (!m_stopRequested)
		{
			const GameTime frameStartMs = GetAsyncTimeMs();
			if (endMs != 0 && frameStartMs >= endMs)
			{
				break;
			}

			// One poll for every session in the process. Polling per session would be the same
			// work; sleeping per session is what would turn a 50ms frame into a several-second
			// one once there are a hundred bots.
			m_io.poll();

			StartPendingLogins(frameStartMs);

			uint32 online = 0;
			for (auto& bot : m_bots)
			{
				UpdateBot(*bot, frameStartMs);
				if (bot->session->IsWorldReady())
				{
					++online;
				}
			}

			SwarmCounters& counters = m_telemetry.GetCounters();
			counters.peakOnline = std::max(counters.peakOnline, online);

			const GameTime elapsedMs = GetAsyncTimeMs() - frameStartMs;
			if (elapsedMs < m_settings.tickMs)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(m_settings.tickMs - elapsedMs));
			}
		}

		ILOG("Swarm stopping, shutting down " << m_bots.size() << " sessions");

		for (auto& bot : m_bots)
		{
			if (bot->loginStarted)
			{
				bot->session->Shutdown();
			}
		}

		// Give the sockets a moment to actually close. A swarm that drops its connections without
		// a word leaves the realm holding sessions it still believes are live, and the next run
		// then collides with its own ghosts.
		for (int i = 0; i < 20; ++i)
		{
			m_io.poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		m_telemetry.WriteSummary();
	}
}
