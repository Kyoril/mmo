// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_login_connector.h"
#include "bot_realm_connector.h"

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
					{ "heartbeat_ms", outConfig.heartbeatMs }
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
			const auto heartbeatInterval = std::chrono::milliseconds(m_config.heartbeatMs);
			while (!m_stopRequested)
			{
				m_io.poll();

				if (m_worldReady && !m_sentGreeting && !m_config.greeting.empty())
				{
					m_realm->SendChatMessage(m_config.greeting, ChatType::Say, "");
					m_sentGreeting = true;
					m_lastHeartbeat = std::chrono::steady_clock::now();
				}

				if (m_worldReady && m_config.heartbeatMs > 0)
				{
					const auto now = std::chrono::steady_clock::now();
					if (now - m_lastHeartbeat >= heartbeatInterval)
					{
						SendHeartbeat();
						m_lastHeartbeat = now;
					}
				}

				std::this_thread::sleep_for(50ms);
			}

			m_realm->close();
			m_login->close();
		}

	private:
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
					m_worldReady = true;
				});

			m_realm->Disconnected.connect([this]()
				{
					ELOG("Realm connection lost.");
					m_stopRequested = true;
				});
		}

		void SendHeartbeat()
		{
			if (m_realm->GetSelectedGuid() == 0)
			{
				return;
			}

			MovementInfo info = m_realm->GetMovementInfo();
			info.timestamp = GetAsyncTimeMs();
			m_realm->SendMovementUpdate(m_realm->GetSelectedGuid(), game::client_realm_packet::MoveHeartBeat, info);
		}

	private:
		BotConfig m_config;
		asio::io_service m_io;
		std::shared_ptr<BotLoginConnector> m_login;
		std::shared_ptr<BotRealmConnector> m_realm;
		bool m_stopRequested { false };
		bool m_worldReady { false };
		bool m_sentGreeting { false };
		bool m_realmConnectionAttempted { false };
		std::chrono::steady_clock::time_point m_lastHeartbeat { std::chrono::steady_clock::now() };
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
