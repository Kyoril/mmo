// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_login_connector.h"
#include "bot_realm_connector.h"
#include "bot_context.h"
#include "bot_profile.h"
#include "bot_profiles.h"
#include "bot_unit_watcher.h"

#include "base/clock.h"
#include "base/typedefs.h"
#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "asio/io_service.hpp"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

using namespace std::chrono_literals;

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace mmo
{
	struct BotConfig
	{
		std::string loginHost { "mmo-dev.net" };
		uint16 loginPort { constants::DefaultLoginPlayerPort };
		std::string username;
		std::string password;

		std::string realmName;
		size_t realmIndex { 0 };

		std::string characterName { "Bot" };
		bool createCharacter { false };
		uint8 race { 0 };
		uint8 characterClass { 1 };
		uint8 gender { 0 };

		std::string greeting { "Hi" };
		bool randomMove { false };
		uint32 heartbeatMs { 5000 };
		
		// Profile configuration
		std::string profileName { "simple_greeter" };
	};

	bool LoadConfig(const fs::path& path, BotConfig& outConfig)
	{
		if (!fs::exists(path))
		{
			json sample = {
				{ "login", {
					{ "host", outConfig.loginHost },
					{ "port", outConfig.loginPort },
					{ "username", "your-account" },
					{ "password", "your-password" }
				}},
				{ "realm", {
					{ "name", "Development" },
					{ "index", 0 }
				}},
				{ "character", {
					{ "name", outConfig.characterName },
					{ "create_if_missing", true },
					{ "race", outConfig.race },
					{ "class", outConfig.characterClass },
					{ "gender", outConfig.gender }
				}},
				{ "behavior", {
					{ "greeting", outConfig.greeting },
					{ "random_move", outConfig.randomMove },
					{ "heartbeat_ms", outConfig.heartbeatMs },
					{ "profile", outConfig.profileName }
				}}
			};

			std::ofstream sampleFile(path);
			if (sampleFile)
			{
				sampleFile << sample.dump(4);
				ILOG("Created sample bot config at " << path << ". Please fill in your account details.");
			}
			else
			{
				ELOG("Could not create config file at " << path);
			}
			return false;
		}

		std::ifstream file(path);
		if (!file)
		{
			ELOG("Failed to open bot config file: " << path);
			return false;
		}

		json data;
		file >> data;

		const auto& login = data.value("login", json::object());
		outConfig.loginHost = login.value("host", outConfig.loginHost);
		outConfig.loginPort = login.value("port", outConfig.loginPort);
		outConfig.username = login.value("username", outConfig.username);
		outConfig.password = login.value("password", outConfig.password);

		const auto& realm = data.value("realm", json::object());
		outConfig.realmName = realm.value("name", outConfig.realmName);
		outConfig.realmIndex = realm.value("index", outConfig.realmIndex);

		const auto& character = data.value("character", json::object());
		outConfig.characterName = character.value("name", outConfig.characterName);
		outConfig.createCharacter = character.value("create_if_missing", outConfig.createCharacter);
		outConfig.race = character.value("race", outConfig.race);
		outConfig.characterClass = character.value("class", outConfig.characterClass);
		outConfig.gender = character.value("gender", outConfig.gender);

		const auto& behavior = data.value("behavior", json::object());
		outConfig.greeting = behavior.value("greeting", outConfig.greeting);
		outConfig.randomMove = behavior.value("random_move", outConfig.randomMove);
		outConfig.heartbeatMs = behavior.value("heartbeat_ms", outConfig.heartbeatMs);
		outConfig.profileName = behavior.value("profile", outConfig.profileName);

		return true;
	}

	void InitializeLogging()
	{
		static std::mutex coutLogMutex;
		auto options = g_DefaultConsoleLogOptions;
		options.alwaysFlush = true;

		g_DefaultLog.signal().connect([options](const LogEntry& entry) mutable
			{
				static std::mutex coutLogMutex;
				std::scoped_lock lock{ coutLogMutex };
				printLogEntry(std::cout, entry, options);
			});
	}

	bool EqualsIgnoreCase(const std::string& a, const std::string& b)
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

	class BotSession
	{
	public:
		explicit BotSession(BotConfig config)
			: m_config(std::move(config))
			, m_io()
			, m_login(std::make_shared<BotLoginConnector>(m_io, m_config.loginHost, m_config.loginPort))
			, m_realm(std::make_shared<BotRealmConnector>(m_io))
			, m_context(std::make_shared<BotContext>(m_realm, m_config))
			, m_profile(CreateProfile())
		{
			BindSignals();
		}

		bool Start()
		{
			if (m_config.username.empty() || m_config.password.empty())
			{
				ELOG("Missing username or password in bot_config.json");
				return false;
			}

			m_login->Connect(m_config.username, m_config.password);
			return true;
		}

		void Run()
		{
			while (!m_stopRequested)
			{
				m_io.poll();

				// Update bot profile if world is ready
				if (m_worldReady && m_profile)
				{
					if (!m_profileActivated)
					{
						ILOG("Activating bot profile: " << m_profile->GetName());
						m_context->SetWorldReady(true);
						m_profile->OnActivate(*m_context);
						m_profileActivated = true;
					}

					// Update area watcher with bot's current position
					if (m_areaWatcher)
					{
						auto* self = m_realm->GetObjectManager().GetSelf();
						if (self)
						{
							m_areaWatcher->SetCenter(self->GetPosition());
						}
						m_areaWatcher->Update();
					}

					if (!m_profile->Update(*m_context))
					{
						ILOG("Bot profile completed execution");
						// Profile finished, but keep connection alive
					}
				}

				std::this_thread::sleep_for(50ms);
			}

			if (m_profile && m_profileActivated)
			{
				m_profile->OnDeactivate(*m_context);
			}

			m_realm->close();
			m_login->close();
		}

	private:
		BotProfilePtr CreateProfile()
		{
			// Create profile based on configuration
			if (m_config.profileName == "simple_greeter")
			{
				return std::make_shared<SimpleGreeterProfile>(m_config.greeting);
			}
			else if (m_config.profileName == "chatter")
			{
				// Example: Create a chatter bot with some test messages
				std::vector<std::string> messages = {
					"Hello!",
					"How is everyone doing?",
					"I'm a test bot!",
					"This is pretty cool!"
				};
				return std::make_shared<ChatterProfile>(messages, 3000ms);
			}
			else if (m_config.profileName == "sequence")
			{
				return std::make_shared<SequenceProfile>();
			}
			else if (m_config.profileName == "unit_awareness")
			{
				return std::make_shared<UnitAwarenessProfile>();
			}
			else
			{
				WLOG("Unknown profile '" << m_config.profileName << "', using simple_greeter");
				return std::make_shared<SimpleGreeterProfile>(m_config.greeting);
			}
		}

		void BindSignals()
		{
			m_login->AuthenticationResult.connect([this](auth::AuthResult result)
				{
					if (result != auth::auth_result::Success)
					{
						ELOG("Authentication at login server failed with code " << static_cast<int32>(result));
						m_stopRequested = true;
						return;
					}

					ILOG("Authenticated at login server.");
					m_login->SendRealmListRequest();
				});

			m_login->RealmListUpdated.connect([this]()
				{
					// Only connect once - prevent double connection attempts
					if (m_realmConnectionAttempted)
					{
						return;
					}
					m_realmConnectionAttempted = true;

					const auto& realms = m_login->GetRealms();
					if (realms.empty())
					{
						ELOG("No realms available.");
						m_stopRequested = true;
						return;
					}

					const RealmData* chosenRealm = nullptr;
					if (!m_config.realmName.empty())
					{
						auto it = std::find_if(realms.begin(), realms.end(), [this](const RealmData& realm)
							{
								return EqualsIgnoreCase(realm.name, m_config.realmName);
							});
						if (it != realms.end())
						{
							chosenRealm = &(*it);
						}
					}

					if (!chosenRealm)
					{
						if (m_config.realmIndex < realms.size())
						{
							chosenRealm = &realms[m_config.realmIndex];
						}
						else
						{
							chosenRealm = &realms.front();
						}
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
						m_stopRequested = true;
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
					const CharacterView* selected = nullptr;
					auto findName = [this](const CharacterView& view)
					{
						return EqualsIgnoreCase(view.GetName(), m_config.characterName);
					};

					auto existing = std::find_if(views.begin(), views.end(), findName);
					if (existing != views.end())
					{
						selected = &(*existing);
						ILOG("Using existing character \"" << selected->GetName() << "\".");
						m_realm->EnterWorld(*selected);
						return;
					}

					if (m_config.createCharacter)
					{
						ILOG("Character \"" << m_config.characterName << "\" not found. Creating a new one...");
						AvatarConfiguration avatarConfig;
						m_realm->CreateCharacter(m_config.characterName, m_config.race, m_config.characterClass, m_config.gender, avatarConfig);
						return;
					}

					ELOG("Character \"" << m_config.characterName << "\" not found and auto-creation disabled.");
					m_stopRequested = true;
				});

			m_realm->CharacterCreated.connect([this](game::CharCreateResult result)
				{
					if (result != game::char_create_result::Unknown)
					{
						ELOG("Character creation failed with code " << static_cast<int32>(result));
						m_stopRequested = true;
						return;
					}

					m_realm->RequestCharEnum();
				});

			m_realm->EnterWorldFailed.connect([this](game::player_login_response::Type reason)
				{
					ELOG("Enter world failed, reason code " << static_cast<int32>(reason));
					m_stopRequested = true;
				});

			m_realm->VerifyNewWorld.connect([this](uint32 mapId, Vector3 position, float facing)
				{
					ILOG("Entered world on map " << mapId << " at position (" << position.x << ", " << position.y << ", " << position.z << ").");
					
					// Initialize cached movement info from realm connector
					m_context->UpdateMovementInfo(m_realm->GetMovementInfo());
					
					// Create the area watcher centered on bot's position with 40 yard radius
					auto& objectManager = m_realm->GetObjectManager();
					m_areaWatcher = std::make_unique<BotUnitWatcher>(objectManager, position, 40.0f);
					
					// Wire up area watcher events to profile
					m_areaWatcher->UnitEntered.connect([this](const BotUnit& unit)
						{
							if (m_profile && m_profileActivated)
							{
								m_profile->OnUnitEnteredArea(*m_context, unit);
							}
						});
					
					m_areaWatcher->UnitLeft.connect([this](uint64 guid)
						{
							if (m_profile && m_profileActivated)
							{
								m_profile->OnUnitLeftArea(*m_context, guid);
							}
						});
					
					m_worldReady = true;
				});

			m_realm->Disconnected.connect([this]()
				{
					ELOG("Realm connection lost.");
					m_stopRequested = true;
				});

			m_realm->PartyInvitationReceived.connect([this](const std::string& inviterName)
				{
					if (!m_profile || !m_profileActivated)
					{
						WLOG("Received party invitation from " << inviterName << " but profile not active yet");
						m_realm->DeclinePartyInvitation();
						return;
					}

					ILOG("Party invitation from " << inviterName << " - delegating to profile");
					const bool shouldAccept = m_profile->OnPartyInvitation(*m_context, inviterName);
					
					if (!shouldAccept)
					{
						ILOG("Profile declined party invitation from " << inviterName);
						m_realm->DeclinePartyInvitation();
					}
					// If profile returns true, it should queue an accept action for realistic delay
				});

			m_realm->PartyJoined.connect([this](uint64 leaderGuid, uint32 memberCount)
				{
					if (!m_profile || !m_profileActivated)
					{
						return;
					}

					m_profile->OnPartyJoined(*m_context, leaderGuid, memberCount);
				});

			m_realm->PartyLeft.connect([this]()
				{
					if (!m_profile || !m_profileActivated)
					{
						return;
					}

					m_profile->OnPartyLeft(*m_context);
				});

			// Unit awareness signals from object manager
			auto& objectManager = m_realm->GetObjectManager();
			
			objectManager.UnitSpawned.connect([this](const BotUnit& unit)
				{
					if (!m_profile || !m_profileActivated)
					{
						return;
					}

					m_profile->OnUnitSpawned(*m_context, unit);
					
					// Update the area watcher if it exists
					if (m_areaWatcher)
					{
						// Reposition watcher to bot's current position
						auto* self = m_realm->GetObjectManager().GetSelf();
						if (self)
						{
							m_areaWatcher->SetCenter(self->GetPosition());
						}
						m_areaWatcher->Update();
					}
				});

			objectManager.UnitDespawned.connect([this](uint64 guid)
				{
					if (!m_profile || !m_profileActivated)
					{
						return;
					}

					m_profile->OnUnitDespawned(*m_context, guid);
					
					// Area watcher will automatically handle this via its own signal connection
				});
		}

	private:
		BotConfig m_config;
		asio::io_service m_io;
		std::shared_ptr<BotLoginConnector> m_login;
		std::shared_ptr<BotRealmConnector> m_realm;
		std::shared_ptr<BotContext> m_context;
		BotProfilePtr m_profile;
		std::unique_ptr<BotUnitWatcher> m_areaWatcher;
		bool m_stopRequested { false };
		bool m_worldReady { false };
		bool m_profileActivated { false };
		bool m_realmConnectionAttempted { false };
	};
}

int main()
{
	using namespace mmo;

	InitializeLogging();

	const fs::path configPath = "bot_config.json";
	BotConfig config;
	if (!LoadConfig(configPath, config))
	{
		return 1;
	}

	BotSession session(config);
	if (!session.Start())
	{
		return 1;
	}

	session.Run();
	return 0;
}
