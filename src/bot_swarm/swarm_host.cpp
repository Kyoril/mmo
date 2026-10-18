// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "swarm_host.h"

#include "bot_ai/strategies/grind_strategy.h"
#include "bot_chat/anthropic_chat_model.h"
#include "bot_chat/offline_chat_model.h"
#include "bot_core/bot_movement_controller.h"
#include "bot_core/io_poll.h"
#include "bot_core/bot_realm_connector.h"
#include "bot_core/bot_unit.h"

#include "base/clock.h"

#include "asio/executor_work_guard.hpp"
#include "log/default_log_levels.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iterator>
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

		/// Backoff before bringing a dropped bot back, doubling per attempt up to the cap. A
		/// population meant to run for days would otherwise drain away one disconnect at a time,
		/// and reconnecting instantly would hammer a server that is down for a reason.
		constexpr GameTime ReconnectBaseDelayMs = 5000;
		constexpr GameTime ReconnectMaxDelayMs = 60000;
		constexpr uint32 ReconnectMaxBackoffSteps = 4;

		/// How long a bot may take to get from starting a login to being in the world. Generous:
		/// character creation on first login plus a busy realm is several seconds, and the cost of
		/// being wrong is a needless reconnect rather than a lost bot.
		constexpr GameTime WorldEntryTimeoutMs = 60000;

		/// How long a chat line waits for its sender's name before it is dropped.
		constexpr GameTime ChatNameTimeoutMs = 10000;

		/// How often each bot looks around for real players. Seeing one is what allows bots to
		/// start conversations; without anyone around, unprompted chat would be spent on bots.
		constexpr GameTime HumanScanIntervalMs = 10000;
		constexpr float HumanScanRadius = 60.0f;
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

		if (m_settings.chatEnabled && !SetupChat(*project))
		{
			return false;
		}

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

			if (m_chatDirector)
			{
				bot->chat = std::make_unique<SwarmChatParticipant>(*bot->session, *bot->brain, bot->roster, m_navService->GetProject());
				m_chatDirector->AddBot(entry.index, *bot->chat, entry.seed ^ 0x5bd1e995u);
			}

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
					if (m_chatDirector)
					{
						m_chatDirector->NoteEvent(index, "you just died", GetAsyncTimeMs());
					}
				}

				m_telemetry.Event(index, "state_change",
					{ { "from", GetBotAiStateName(from) }, { "to", GetBotAiStateName(to) } });
			}));

		bot.connections.emplace_back(bot.brain->TargetKilled.connect(
			[this, index, botPtr = &bot](const uint64 guid)
			{
				++m_telemetry.GetCounters().kills;
				if (m_chatDirector)
				{
					const BotUnit* victim = botPtr->session->GetContext().GetUnit(guid);
					if (victim && !victim->GetName().empty())
					{
						m_chatDirector->NoteEvent(index, "you just killed a " + victim->GetName(), GetAsyncTimeMs());
					}
				}
				m_telemetry.Event(index, "kill", { { "target", guid } });
			}));

		bot.connections.emplace_back(bot.session->GetRealm().VerifyNewWorld.connect(
			[this, index, botPtr = &bot](const uint32 mapId, const Vector3& position, const float facing)
			{
				// The same signal announces the first entry into the world and every later hand-off
				// between world nodes. Only the later ones are transfers; wasInWorld still reflects
				// the previous frame here, because the packet arrives before the bot is updated.
				if (!botPtr->wasInWorld)
				{
					return;
				}

				++m_telemetry.GetCounters().worldTransfers;
				m_telemetry.Event(index, "world_change",
					{ { "map", mapId }, { "x", position.x }, { "y", position.y }, { "z", position.z } });
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

		if (m_chatDirector)
		{
			bot.connections.emplace_back(bot.session->GetRealm().ChatMessageReceived.connect(
				[this, botPtr = &bot](const BotChatMessage& message)
				{
					OnBotChat(*botPtr, message);
				}));

			bot.connections.emplace_back(bot.session->GetRealm().NameResolved.connect(
				[this, botPtr = &bot](const uint64 guid, const std::string& name)
				{
					auto& waiting = botPtr->chatAwaitingName;
					for (auto it = waiting.begin(); it != waiting.end();)
					{
						if (it->first.senderGuid == guid)
						{
							const BotChatMessage message = it->first;
							it = waiting.erase(it);
							DeliverChat(*botPtr, message, name);
						}
						else
						{
							++it;
						}
					}
				}));
		}

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
		bot.worldEntryDeadlineMs = nowMs + WorldEntryTimeoutMs;
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
		bot.worldEntryDeadlineMs = 0;
		++m_telemetry.GetCounters().enteredWorld;
		m_telemetry.Event(bot.roster.index, "enter_world",
			{ { "character", bot.roster.characterName }, { "level", bot.roster.targetLevel } });

		bot.brain->Reset();

		bot.generalJoinRequested = false;
		m_botGuids.insert(bot.session->GetContext().GetSelectedCharacterGuid());

		if (m_settings.applyLevelRoll && !bot.levelApplied && bot.roster.targetLevel > 1)
		{
			// Levelling up front rather than starting everyone at one is what puts the whole
			// level range of the game under load at once, instead of only its first hour.
			const uint32 levels = bot.roster.targetLevel - 1;
			bot.session->GetRealm().CheatLevelUp(static_cast<uint8>(std::min<uint32>(levels, 255)));
			bot.levelApplied = true;
		}
	}

	void SwarmHost::ScheduleReconnect(Bot& bot, const GameTime nowMs)
	{
		const uint32 step = std::min(bot.reconnectAttempts, ReconnectMaxBackoffSteps);
		const GameTime delay = std::min(ReconnectMaxDelayMs, ReconnectBaseDelayMs << step);
		bot.reconnectAtMs = nowMs + delay;
	}

	void SwarmHost::ServiceReconnect(Bot& bot, const GameTime nowMs)
	{
		if (bot.reconnectAtMs == 0 || nowMs < bot.reconnectAtMs)
		{
			return;
		}

		bot.reconnectAtMs = 0;
		++bot.reconnectAttempts;
		++m_telemetry.GetCounters().reconnects;

		// Cleared so that a failure after this attempt is reported as its own event rather than
		// being swallowed as already-known.
		bot.failureReported = false;

		m_telemetry.Event(bot.roster.index, "reconnect",
			{ { "attempt", bot.reconnectAttempts }, { "account", bot.roster.account } });

		// Deliberately the non-blocking form. Reconnect() pumps and sleeps until the character is
		// back, which in a host driving many sessions would stall every other bot for the duration.
		bot.session->BeginReconnect();
		bot.worldEntryDeadlineMs = nowMs + WorldEntryTimeoutMs;
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
			// The message has to say which it was: a bot that never got in and a bot that was
			// dropped after playing for ten minutes are entirely different problems, and reporting
			// both as the former sends the reader looking in the wrong place.
			m_telemetry.Event(bot.roster.index, "error",
				{ { "message", bot.wasInWorld
					? "session stopped after entering the world"
					: "session stopped before entering the world" },
				  { "exit_code", static_cast<int32>(bot.session->GetExitCode()) },
				  { "account", bot.roster.account },
				  { "character", bot.roster.characterName } });

			ScheduleReconnect(bot, nowMs);
		}

		// A session that neither reaches the world nor gives up is the one failure mode nothing
		// else here can see: IsStopRequested stays false, IsWorldReady stays false, and the bot
		// simply never appears again. It has to be timed out explicitly.
		if (!bot.session->IsWorldReady()
			&& bot.worldEntryDeadlineMs != 0
			&& nowMs >= bot.worldEntryDeadlineMs)
		{
			bot.worldEntryDeadlineMs = 0;
			++m_telemetry.GetCounters().errors;
			m_telemetry.Event(bot.roster.index, "error",
				{ { "message", "did not reach the world within the deadline" },
				  { "account", bot.roster.account },
				  { "character", bot.roster.characterName } });

			ScheduleReconnect(bot, nowMs);
		}

		ServiceReconnect(bot, nowMs);

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
			if (m_chatDirector)
			{
				m_chatDirector->NoteEvent(bot.roster.index, "you just reached level " + std::to_string(level), nowMs);
			}
		}

		bot.lastKnownLevel = level;

		bot.brain->Update(nowMs);
		CheckWatchdog(bot, nowMs);

		if (m_chatDirector)
		{
			UpdateBotChat(bot, nowMs);
		}
	}

	bool SwarmHost::SetupChat(const proto::Project& project)
	{
		ChatPricing pricing;
		if (!LookupChatPricing(m_settings.chatModel, pricing))
		{
			ELOG("Bot chat: no known price for model '" << m_settings.chatModel
				<< "', so its spend could not be capped. Use a model listed in bot_chat/chat_budget.cpp.");
			return false;
		}

		if (m_settings.chatOffline)
		{
			// The canned model costs nothing; it must not use up the real ledger's hourly cap either.
			m_chatBudget = std::make_unique<ChatBudget>(m_settings.chatLimits, pricing, std::string());
			m_chatModel = std::make_unique<OfflineChatModel>(0xC0FFEEu);
		}
		else
		{
			if (m_settings.chatApiKey.empty())
			{
				ELOG("Bot chat: neither " << ChatApiKeyVariable << " nor ANTHROPIC_API_KEY is set. Set one, or pass --chat-offline to try chat without the model.");
				return false;
			}

			if (m_settings.chatLedgerPath.empty())
			{
				ELOG("Bot chat: a spend ledger path is required, or the caps would reset with every restart");
				return false;
			}

			m_chatBudget = std::make_unique<ChatBudget>(m_settings.chatLimits, pricing, m_settings.chatLedgerPath);
			m_chatModel = std::make_unique<AnthropicChatModel>(m_settings.chatApiKey, m_settings.chatModel);
		}

		ChatDirectorSettings settings;
		settings.seed = 0xA11CE5u;
		settings.gameFacts = BuildGameFacts(project);

		if (!m_settings.chatFactsPath.empty())
		{
			std::ifstream file(m_settings.chatFactsPath);
			if (!file)
			{
				ELOG("Bot chat: cannot read facts file " << m_settings.chatFactsPath);
				return false;
			}

			settings.gameFacts += '\n';
			settings.gameFacts.append(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}

		m_chatDirector = std::make_unique<ChatDirector>(settings, *m_chatModel, *m_chatBudget,
			[]() { return std::time(nullptr); });

		m_chatConnections.emplace_back(m_chatDirector->ChatSent.connect(
			[this](const uint32 botIndex, const ChatType type, const std::string& target, const std::string& text)
			{
				m_telemetry.Event(botIndex, "chat_out",
					{ { "chat_type", static_cast<uint32>(type) }, { "target", target }, { "text", text } });
			}));

		m_chatConnections.emplace_back(m_chatDirector->RequestFailed.connect(
			[this](const uint32 botIndex, const std::string& reason)
			{
				m_telemetry.Event(botIndex, "chat_error", { { "reason", reason } });
			}));

		ILOG("Bot chat enabled: model " << m_settings.chatModel << (m_settings.chatOffline ? " (offline, canned lines)" : "")
			<< ", caps $" << m_settings.chatLimits.dailyUsd << "/day, $" << m_settings.chatLimits.monthlyUsd
			<< "/month, " << m_settings.chatLimits.maxRequestsPerHour << " requests/hour; spent today so far $"
			<< m_chatBudget->GetSpentToday());
		return true;
	}

	void SwarmHost::OnBotChat(Bot& bot, const BotChatMessage& message)
	{
		if (message.senderGuid == 0 || message.type == ChatType::System || message.type == ChatType::TextEmote)
		{
			return;
		}

		BotRealmConnector& realm = bot.session->GetRealm();
		if (const std::string* name = realm.GetCachedName(message.senderGuid))
		{
			DeliverChat(bot, message, *name);
			return;
		}

		bot.chatAwaitingName.emplace_back(message, GetAsyncTimeMs());
		realm.QueryName(message.senderGuid);
	}

	void SwarmHost::DeliverChat(Bot& bot, const BotChatMessage& message, const std::string& senderName)
	{
		++m_telemetry.GetCounters().chatLinesHeard;
		m_chatDirector->OnChatReceived(bot.roster.index, message.type, senderName, message.message,
			message.channelId, GetAsyncTimeMs());
	}

	void SwarmHost::UpdateBotChat(Bot& bot, const GameTime nowMs)
	{
		BotRealmConnector& realm = bot.session->GetRealm();

		// Characters are normally put into General on login. One that is not joins it, once per
		// session; a realm without a General channel answers DoesNotExist and that is the end of it.
		if (!bot.generalJoinRequested && realm.HasChatChannelList() && realm.FindChatChannelId(GeneralChannelName) == 0)
		{
			realm.JoinChatChannel(GeneralChannelName);
			bot.generalJoinRequested = true;
		}

		auto& waiting = bot.chatAwaitingName;
		waiting.erase(std::remove_if(waiting.begin(), waiting.end(), [nowMs](const auto& entry)
			{
				return nowMs - entry.second > ChatNameTimeoutMs;
			}), waiting.end());

		if (nowMs < bot.nextHumanScanMs)
		{
			return;
		}

		bot.nextHumanScanMs = nowMs + HumanScanIntervalMs;
		for (const BotUnit* player : bot.session->GetContext().GetNearbyPlayers(HumanScanRadius))
		{
			if (m_botGuids.count(player->GetGuid()) == 0)
			{
				m_chatDirector->NoteHumanSeen(nowMs);
				break;
			}
		}
	}

	void SwarmHost::Run()
	{
		const GameTime startMs = GetAsyncTimeMs();
		const GameTime endMs = m_settings.durationSeconds > 0
			? startMs + static_cast<GameTime>(m_settings.durationSeconds) * 1000
			: 0;

		// Without this the very first poll finds no outstanding work - no bot has logged in yet -
		// and asio's scheduler::poll answers that by calling stop() on itself. Nothing here restarts
		// it, so the io service stays latched stopped for the life of the process. The IOCP backend
		// dispatches completions anyway, which is why this survives on Windows; the POSIX scheduler
		// returns immediately from do_poll_one while stopped, so the swarm would never run a single
		// handler on the Linux build - where MMO_BUILD_BOTS is on by default.
		auto workGuard = std::make_unique<asio::executor_work_guard<asio::io_service::executor_type>>(
			asio::make_work_guard(m_io));

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
			PollRestarting(m_io);

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

			if (m_chatDirector)
			{
				m_chatDirector->Update(frameStartMs);
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

		if (m_chatModel)
		{
			// Joins the worker. A request in flight is abandoned; its reservation stays charged.
			m_chatModel->Stop();

			const ChatDirectorStats& chat = m_chatDirector->GetStats();
			SwarmCounters& counters = m_telemetry.GetCounters();
			counters.chatLinesSent = chat.linesSent;
			counters.chatRequests = chat.requestsSubmitted;
			counters.chatBudgetDenied = chat.budgetDenied;
			counters.chatModelErrors = chat.modelErrors;
			counters.chatSpentTodayUsd = m_chatBudget->GetSpentToday();
		}

		for (auto& bot : m_bots)
		{
			if (bot->loginStarted)
			{
				bot->session->Shutdown();
			}
		}

		// Released so the drain below can finish: with the guard held, work never reaches zero and
		// the loop would have nothing to tell it the sockets are done.
		workGuard.reset();

		// Give the sockets a moment to actually close. A swarm that drops its connections without
		// a word leaves the realm holding sessions it still believes are live, and the next run
		// then collides with its own ghosts.
		for (int i = 0; i < 20; ++i)
		{
			PollRestarting(m_io);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		m_telemetry.WriteSummary();
	}
}
