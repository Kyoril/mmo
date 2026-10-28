// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_session.h"

#include "bot_startup_selection.h"
#include "io_poll.h"

#include "base/macros.h"
#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace mmo
{
	namespace
	{
		bool equalsIgnoreCase(const std::string& a, const std::string& b)
		{
			if (a.size() != b.size())
			{
				return false;
			}

			for (size_t i = 0; i < a.size(); ++i)
			{
				if (::tolower(a[i]) != ::tolower(b[i]))
				{
					return false;
				}
			}

			return true;
		}
	}

	BotSession::BotSession(asio::io_service& io, std::shared_ptr<BotNavService> navService, BotConfig config)
		: m_config(std::move(config))
		, m_io(io)
		, m_login(std::make_shared<BotLoginConnector>(m_io, m_config.loginHost, m_config.loginPort))
		, m_realm(std::make_shared<BotRealmConnector>(m_io))
		, m_navService(std::move(navService))
		, m_context(std::make_shared<BotContext>(m_realm, m_config, m_navService))
	{
		ASSERT(m_navService);
		BindSignals();
	}

	void BotSession::Update()
	{
		if (!m_worldReady)
		{
			return;
		}

		// Path following is the only thing a session advances on its own. Doing it here rather
		// than inside the blocking MoveTo call is what lets a bot keep walking while its owner is
		// doing something else - an AI deciding on its next action, or a scenario waiting on a
		// condition.
		static_cast<void>(m_movementController.Update(*m_context));
	}

	bool BotSession::Start()
	{
		if (m_config.username.empty() || m_config.password.empty())
		{
			ELOG("Missing account name or password in e2e client config");
			return false;
		}

		m_login->Connect(m_config.username, m_config.password);
		return true;
	}

	void BotSession::BeginReconnect()
	{
		ILOG("Reconnecting the session for account " << m_config.username);

		// Everything the previous session left behind. The objects it saw are gone -- the world
		// will send fresh spawns -- and the flags that mark "we are in" have to fall back or the
		// signal handlers below will short-circuit.
		m_realm->GetObjectManager().Clear();
		m_context->SetWorldReady(false);
		m_worldReady = false;
		m_realmConnectionAttempted = false;
		m_disconnected = false;

		// A reconnect is a fresh session: whatever tolerance the scenario armed for the previous
		// disconnect does not carry over, so a drop from here on fails the run again unless the
		// scenario asks for it a second time.
		m_disconnectExpected = false;

		// And a previous failure must not outlive it either. Fail() latched an exit code and the
		// stop flag, and every caller treats those as terminal - without clearing them a session
		// that reconnects after a dropped connection would be considered dead the moment it came
		// back.
		m_stopRequested = false;
		m_exitCode = bot_exit_code::Success;
		m_failureStage = bot_failure_stage::None;

		m_login->Connect(m_config.username, m_config.password);
	}

	void BotSession::BeginLogoutCycle()
	{
		if (!m_worldReady)
		{
			return;
		}

		m_realm->Logout();
	}

	void BotSession::BeginReroll(const std::string& newName, const uint8 race, const uint8 characterClass, const uint8 gender)
	{
		ILOG("Retiring character \"" << m_config.characterName << "\" of account " << m_config.username << " for \"" << newName << "\"");

		m_retireCharacter = m_config.characterName;
		m_retireAttempts = 0;
		m_nameAttempts = 0;
		m_config.characterName = newName;
		m_config.race = race;
		m_config.characterClass = characterClass;
		m_config.gender = gender;
		m_config.createCharacter = true;

		// Out of the world on purpose: the brain stops at once, and the closed connection is not
		// a failure.
		m_context->SetWorldReady(false);
		m_worldReady = false;
		m_disconnectExpected = true;
		m_realm->close();
		m_login->close();
	}

	bot_exit_code::Type BotSession::Reconnect(const uint32 timeoutSeconds)
	{
		BeginReconnect();
		return WaitForWorld(timeoutSeconds);
	}

	bot_exit_code::Type BotSession::WaitForWorld(const uint32 timeoutSeconds)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
		while (!m_worldReady)
		{
			if (m_stopRequested)
			{
				return m_exitCode;
			}
			if (std::chrono::steady_clock::now() >= deadline)
			{
				ELOG("Timed out after " << timeoutSeconds << "s waiting to enter the world");
				return bot_exit_code::Timeout;
			}

			Pump();
		}

		return bot_exit_code::Success;
	}

	bot_exit_code::Type BotSession::RunSmoke(const uint32 seconds)
	{
		const BotUnit* self = m_realm->GetObjectManager().GetSelf();
		if (!self)
		{
			ELOG("Entered world but self unit was never spawned");
			return bot_exit_code::SetupFailed;
		}

		ILOG("Smoke: self guid=0x" << std::hex << self->GetGuid() << std::dec
			<< " level=" << self->GetLevel()
			<< " health=" << self->GetHealth() << "/" << self->GetMaxHealth()
			<< " position=(" << self->GetPosition().x << ", " << self->GetPosition().y << ", " << self->GetPosition().z << ")");

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (m_stopRequested)
			{
				return m_exitCode;
			}

			Pump();
		}

		ILOG("Smoke: session survived " << seconds << "s - success");
		return bot_exit_code::Success;
	}

	void BotSession::Pump()
	{
		PollRestarting(m_io);
		Update();
		std::this_thread::sleep_for(10ms);
	}

	void BotSession::Shutdown()
	{
		m_shuttingDown = true;
		m_realm->close();
		m_login->close();
		PollRestarting(m_io);
	}

	void BotSession::Fail(const bot_exit_code::Type code, const bot_failure_stage::Type stage)
	{
		m_exitCode = code;
		m_failureStage = stage;
		m_stopRequested = true;
	}

	void BotSession::BindSignals()
	{
		m_login->AuthenticationResult.connect([this](auth::AuthResult result)
			{
				if (result != auth::auth_result::Success)
				{
					ELOG("Authentication at login server failed with code " << static_cast<int32>(result));
					// A connection the login server never accepted is reported as this same result.
					Fail(bot_exit_code::SetupFailed, result == auth::auth_result::FailInvalidServer
						? bot_failure_stage::LoginConnect : bot_failure_stage::LoginAuth);
					return;
				}

				ILOG("Authenticated at login server.");
				m_login->SendRealmListRequest();
			});

		m_login->RealmListUpdated.connect([this]()
			{
				if (m_realmConnectionAttempted)
				{
					return;
				}
				m_realmConnectionAttempted = true;

				const auto& realms = m_login->GetRealms();
				if (realms.empty())
				{
					ELOG("No realms available.");
					Fail(bot_exit_code::SetupFailed, bot_failure_stage::RealmList);
					return;
				}

				const RealmData* chosenRealm = nullptr;
				if (!m_config.realmName.empty())
				{
					const auto it = std::find_if(realms.begin(), realms.end(), [this](const RealmData& realm)
						{
							return equalsIgnoreCase(realm.name, m_config.realmName);
						});
					if (it != realms.end())
					{
						chosenRealm = &(*it);
					}
				}

				if (!chosenRealm)
				{
					ELOG("Realm '" << m_config.realmName << "' not found in realm list.");
					Fail(bot_exit_code::SetupFailed, bot_failure_stage::RealmList);
					return;
				}

				ILOG("Connecting to realm " << chosenRealm->name << " at " << chosenRealm->address << ":" << chosenRealm->port);
				m_realm->SetLoginData(m_login->GetAccountName(), m_login->GetSessionKey());
				m_realm->ConnectToRealm(*chosenRealm);

				// Done with the login server, as a game client is. Left open, the connection is
				// closed by the next reconnect's Connect instead, and its late "lost" callback then
				// clears the packet handlers of the new connection: the handshake never answers and
				// the reconnect hangs until the host times it out.
				m_login->close();
			});

		m_realm->AuthenticationResult.connect([this](uint8 result)
			{
				if (result != auth::auth_result::Success)
				{
					ELOG("Realm authentication failed with code " << static_cast<int32>(result));
					Fail(bot_exit_code::SetupFailed, bot_failure_stage::RealmAuth);
					return;
				}

				ILOG("Authenticated at realm server.");
			});

		m_realm->CharListUpdated.connect([this]()
			{
				if (m_worldReady)
				{
					return;
				}

				const auto& views = m_realm->GetCharacterViews();

				// A retired character goes first. The realm sends the list again once it is gone; a
				// deletion that keeps failing is given up after a few tries rather than looped on.
				if (!m_retireCharacter.empty())
				{
					const auto retired = std::find_if(views.begin(), views.end(), [this](const CharacterView& view)
						{
							return view.GetName() == m_retireCharacter;
						});
					if (retired != views.end() && m_retireAttempts < 3)
					{
						++m_retireAttempts;
						ILOG("Deleting retired character \"" << m_retireCharacter << "\"");
						m_realm->DeleteCharacter(retired->GetGuid());
						return;
					}
					if (retired != views.end())
					{
						WLOG("Could not delete retired character \"" << m_retireCharacter << "\"; keeping it");
					}
					m_retireCharacter.clear();
				}

				const StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
					std::string_view{},
					m_config.characterName,
					m_config.createCharacter,
					views);

				switch (resolution.kind)
				{
				case StartupCharacterResolutionKind::Resolved:
				{
					if (!resolution.resolvedIndex.has_value() || *resolution.resolvedIndex >= views.size())
					{
						ELOG("Character resolution returned an invalid index");
						Fail(bot_exit_code::SetupFailed, bot_failure_stage::Character);
						return;
					}

					const CharacterView& character = views[*resolution.resolvedIndex];
					m_config.characterName = character.GetName();
					m_context->SetCurrentMapId(character.GetMapId());
					ILOG("Entering world with existing character \"" << m_config.characterName << "\" on map " << character.GetMapId());
					m_realm->EnterWorld(character);
					return;
				}
				case StartupCharacterResolutionKind::CreateCharacter:
				{
					if (resolution.selector.empty())
					{
						ELOG("No character name configured for character creation");
						Fail(bot_exit_code::SetupFailed, bot_failure_stage::Character);
						return;
					}

					m_config.characterName = resolution.selector;
					ILOG("Creating character \"" << m_config.characterName << "\" (race " << static_cast<uint32>(m_config.race)
						<< ", class " << static_cast<uint32>(m_config.characterClass)
						<< ", gender " << static_cast<uint32>(m_config.gender) << ")");
					AvatarConfiguration avatarConfig;
					m_realm->CreateCharacter(resolution.selector, m_config.race, m_config.characterClass, m_config.gender, avatarConfig);
					return;
				}
				case StartupCharacterResolutionKind::NeedsPrompt:
				case StartupCharacterResolutionKind::NotFound:
					ELOG("Character \"" << m_config.characterName << "\" not found and character creation is disabled");
					Fail(bot_exit_code::SetupFailed, bot_failure_stage::Character);
					return;
				}
			});

		m_realm->CharacterCreated.connect([this](game::CharCreateResult result)
			{
				// A taken name is tried again under another, as a player would.
				if (result == game::char_create_result::NameInUse && m_nameFallback && m_nameAttempts < 5)
				{
					++m_nameAttempts;
					const std::string previous = m_config.characterName;
					m_config.characterName = m_nameFallback(m_nameAttempts);
					ILOG("Character name \"" << previous << "\" is taken; trying \"" << m_config.characterName << "\"");
					AvatarConfiguration avatarConfig;
					m_realm->CreateCharacter(m_config.characterName, m_config.race, m_config.characterClass, m_config.gender, avatarConfig);
					return;
				}

				if (result != game::char_create_result::Unknown)
				{
					ELOG("Character creation failed with result " << static_cast<int32>(result));
					Fail(bot_exit_code::SetupFailed, bot_failure_stage::Character);
					return;
				}

				ILOG("Character \"" << m_config.characterName << "\" created.");
				m_realm->RequestCharEnum();
			});

		m_realm->EnterWorldFailed.connect([this](game::player_login_response::Type reason)
			{
				m_enterWorldFailure = static_cast<uint8>(reason);

				// The realm says the same when the world node of a character already in the world
				// goes away: it keeps the connection and sends the character back to the list.
				// Without this the session would go on believing it is in the world.
				if (m_worldReady)
				{
					ELOG("Taken out of the world, reason code " << static_cast<int32>(reason));
					m_context->SetWorldReady(false);
					m_worldReady = false;
					Fail(bot_exit_code::Disconnected, bot_failure_stage::WorldLost);

					// A reconnect logs in from the start; the realm connection this one leaves open
					// would be in its way. Closing it is not a second failure.
					m_disconnectExpected = true;
					m_realm->close();
					return;
				}

				ELOG("Enter world failed, reason code " << static_cast<int32>(reason));
				Fail(bot_exit_code::SetupFailed, bot_failure_stage::EnterWorld);
			});

		m_realm->ConnectFailed.connect([this]()
			{
				if (!m_shuttingDown)
				{
					Fail(bot_exit_code::SetupFailed, bot_failure_stage::RealmConnect);
				}
			});

		m_realm->VerifyNewWorld.connect([this](uint32 mapId, Vector3 position, float facing)
			{
				m_context->UpdateCurrentMapIdFromWorldSync(mapId);
				const uint32 effectiveMapId = m_context->GetCurrentMapId();

				ILOG("Entered world on map " << effectiveMapId << " at position ("
					<< position.x << ", " << position.y << ", " << position.z << ")");

				m_context->UpdateMovementInfo(m_realm->GetMovementInfo());
				if (m_navService)
				{
					static_cast<void>(m_navService->EnsureMapAvailable(effectiveMapId));
				}

				// Characters spawn with the FALLING flag set; the server only accepts its
				// removal via MoveFallLand (or a jump). Any other movement packet - including
				// MoveSetFacing - would fail movement validation until this is sent.
				m_context->SendLandedPacket();

				m_context->SetWorldReady(true);
				m_worldReady = true;
			});

		m_realm->Teleported.connect([this]()
			{
				// The server moved the bot - to a graveyard on release, or wherever a spell or a
				// trigger sends it - and now sees it falling there. Movement sent from the old
				// position without the flag would be refused, every packet of it, so the bot takes
				// the new position, lands, and lets whatever was walking plan again from here.
				m_context->UpdateMovementInfo(m_realm->GetMovementInfo());
				m_context->SendLandedPacket();
				m_movementController.Reset();
			});

		m_realm->LoggedOut.connect([this]()
			{
				// Out of the world, connection kept: everything seen there is gone, and the character
				// list brings the character back in (see CharListUpdated).
				ILOG("Logged out to the character list.");
				m_realm->GetObjectManager().Clear();
				m_context->SetWorldReady(false);
				m_worldReady = false;
				m_movementController.Reset();
				m_realm->RequestCharEnum();
			});

		m_realm->SetLiveMovementSource([this]() { return m_context->GetMovementInfo(); });

		m_realm->MovementControlChanged.connect([this](const uint16 ackOpCode, const uint32 ackId, const bool applied)
			{
				// Rooted, stunned, asleep, feared or disoriented: the server moves the bot, if
				// anything does, and takes no movement from it until it lets go. The bot stops where
				// it stands, says so in the ack, and carries the flag in every packet until then.
				MovementInfo info = m_context->GetMovementInfo();
				if (applied)
				{
					info.movementFlags |= movement_flags::Rooted;
					info.movementFlags &= ~movement_flags::Moving;
					m_movementController.Reset();
				}
				else
				{
					info.movementFlags &= ~movement_flags::Rooted;
				}

				info.timestamp = m_context->GetServerTime();
				m_realm->SendMovementControlAck(ackOpCode, ackId, info);
				m_context->UpdateMovementInfo(info);
			});

		m_realm->Disconnected.connect([this]()
			{
				if (m_shuttingDown)
				{
					return;
				}

				// A scenario that provokes the disconnect on purpose records it and carries on
				// checking; only an unexpected one is a failure.
				if (m_disconnectExpected)
				{
					ILOG("Realm connection lost (expected by the scenario).");
					m_disconnected = true;
					return;
				}

				ELOG("Realm connection lost.");

				// Fail only records the exit code, so without this the session goes on reporting that
				// it is in the world for the rest of its life. Everything downstream believes it: the
				// swarm never counts the disconnect, keeps the bot in its online tally, and goes on
				// ticking a brain that pushes packets into a closed socket - and the swarm report's
				// "no unexpected disconnects" check passes because the counter it reads can never move.
				m_context->SetWorldReady(false);
				m_worldReady = false;

				Fail(bot_exit_code::Disconnected, bot_failure_stage::RealmLost);
			});
	}
}
