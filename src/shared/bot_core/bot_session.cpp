// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_session.h"

#include "bot_startup_selection.h"

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

		m_login->Connect(m_config.username, m_config.password);
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
		m_io.poll();
		Update();
		std::this_thread::sleep_for(10ms);
	}

	void BotSession::Shutdown()
	{
		m_shuttingDown = true;
		m_realm->close();
		m_login->close();
		m_io.poll();
	}

	void BotSession::Fail(const bot_exit_code::Type code)
	{
		m_exitCode = code;
		m_stopRequested = true;
	}

	void BotSession::BindSignals()
	{
		m_login->AuthenticationResult.connect([this](auth::AuthResult result)
			{
				if (result != auth::auth_result::Success)
				{
					ELOG("Authentication at login server failed with code " << static_cast<int32>(result));
					Fail(bot_exit_code::SetupFailed);
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
					Fail(bot_exit_code::SetupFailed);
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
					Fail(bot_exit_code::SetupFailed);
					return;
				}

				ILOG("Connecting to realm " << chosenRealm->name << " at " << chosenRealm->address << ":" << chosenRealm->port);
				m_realm->SetLoginData(m_login->GetAccountName(), m_login->GetSessionKey());
				m_realm->ConnectToRealm(*chosenRealm);
			});

		m_realm->AuthenticationResult.connect([this](uint8 result)
			{
				if (result != auth::auth_result::Success)
				{
					ELOG("Realm authentication failed with code " << static_cast<int32>(result));
					Fail(bot_exit_code::SetupFailed);
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
						Fail(bot_exit_code::SetupFailed);
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
						Fail(bot_exit_code::SetupFailed);
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
					Fail(bot_exit_code::SetupFailed);
					return;
				}
			});

		m_realm->CharacterCreated.connect([this](game::CharCreateResult result)
			{
				if (result != game::char_create_result::Unknown)
				{
					ELOG("Character creation failed with result " << static_cast<int32>(result));
					Fail(bot_exit_code::SetupFailed);
					return;
				}

				ILOG("Character \"" << m_config.characterName << "\" created.");
				m_realm->RequestCharEnum();
			});

		m_realm->EnterWorldFailed.connect([this](game::player_login_response::Type reason)
			{
				ELOG("Enter world failed, reason code " << static_cast<int32>(reason));
				Fail(bot_exit_code::SetupFailed);
			});

		m_realm->VerifyNewWorld.connect([this](uint32 mapId, Vector3 position, float facing)
			{
				m_context->UpdateCurrentMapIdFromWorldSync(mapId);
				const uint32 effectiveMapId = m_context->GetCurrentMapId();

				ILOG("Entered world on map " << effectiveMapId << " at position ("
					<< position.x << ", " << position.y << ", " << position.z << ")");

				m_context->UpdateMovementInfo(m_realm->GetMovementInfo());
				if (m_navService && effectiveMapId != 0)
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

				Fail(bot_exit_code::Disconnected);
			});
	}
}
