// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_context.h"
#include "bot_login_connector.h"
#include "bot_nav_service.h"
#include "bot_realm_connector.h"
#include "bot_startup_selection.h"
#include "bot_startup_types.h"

#include "base/clock.h"
#include "base/typedefs.h"
#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "asio/io_service.hpp"

#include "nlohmann/json.hpp"
#include <cxxopts/cxxopts.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace mmo
{
	/// Exit codes of the e2e_client, kept stable so that agents and scripts can rely on them.
	namespace e2e_exit_code
	{
		enum Type
		{
			Success = 0,
			ScenarioFailed = 1,
			SetupFailed = 2,
			Timeout = 3,
			Disconnected = 4,
		};
	}

	bool LoadE2eConfig(const fs::path& path, BotConfig& outConfig)
	{
		std::ifstream file(path);
		if (!file)
		{
			ELOG("Failed to open e2e client config file: " << path);
			return false;
		}

		json data;
		file >> data;

		outConfig.loginHost = data.value("loginHost", outConfig.loginHost);
		outConfig.loginPort = data.value("loginPort", outConfig.loginPort);
		outConfig.username = data.value("accountName", outConfig.username);
		outConfig.password = data.value("accountPassword", outConfig.password);
		outConfig.realmName = data.value("realmName", outConfig.realmName);
		outConfig.characterName = data.value("characterName", outConfig.characterName);
		outConfig.createCharacter = data.value("createCharacter", outConfig.createCharacter);
		outConfig.race = data.value("race", outConfig.race);
		outConfig.characterClass = data.value("class", outConfig.characterClass);
		outConfig.gender = data.value("gender", outConfig.gender);
		return true;
	}

	void InitializeLogging()
	{
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

	/// Headless client session: login -> realm -> character -> enter world, then hand
	/// control to the caller-provided world loop (smoke test or Lua scenario).
	class E2eSession final
	{
	public:
		explicit E2eSession(BotConfig config)
			: m_config(std::move(config))
			, m_io()
			, m_login(std::make_shared<BotLoginConnector>(m_io, m_config.loginHost, m_config.loginPort))
			, m_realm(std::make_shared<BotRealmConnector>(m_io))
			, m_navService(std::make_shared<BotNavService>())
			, m_context(std::make_shared<BotContext>(m_realm, m_config, m_navService))
		{
			BindSignals();
		}

		bool Start()
		{
			if (m_config.username.empty() || m_config.password.empty())
			{
				ELOG("Missing account name or password in e2e client config");
				return false;
			}

			m_login->Connect(m_config.username, m_config.password);
			return true;
		}

		/// Pumps the io service until the character is in the world or the timeout elapses.
		e2e_exit_code::Type WaitForWorld(uint32 timeoutSeconds)
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
					return e2e_exit_code::Timeout;
				}

				m_io.poll();
				std::this_thread::sleep_for(10ms);
			}

			return e2e_exit_code::Success;
		}

		/// Keeps the session alive for the given duration, verifying the connection survives
		/// (time sync responses, spawn handling). Used by --smoke.
		e2e_exit_code::Type RunSmoke(uint32 seconds)
		{
			const BotUnit* self = m_realm->GetObjectManager().GetSelf();
			if (!self)
			{
				ELOG("Entered world but self unit was never spawned");
				return e2e_exit_code::SetupFailed;
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

				m_io.poll();
				std::this_thread::sleep_for(10ms);
			}

			ILOG("Smoke: session survived " << seconds << "s - success");
			return e2e_exit_code::Success;
		}

		void Shutdown()
		{
			m_shuttingDown = true;
			m_realm->close();
			m_login->close();
			m_io.poll();
		}

		BotContext& GetContext() { return *m_context; }
		BotRealmConnector& GetRealm() { return *m_realm; }
		asio::io_service& GetIoService() { return m_io; }
		bool IsStopRequested() const { return m_stopRequested; }
		e2e_exit_code::Type GetExitCode() const { return m_exitCode; }

	private:
		void Fail(e2e_exit_code::Type code)
		{
			m_exitCode = code;
			m_stopRequested = true;
		}

		void BindSignals()
		{
			m_login->AuthenticationResult.connect([this](auth::AuthResult result)
				{
					if (result != auth::auth_result::Success)
					{
						ELOG("Authentication at login server failed with code " << static_cast<int32>(result));
						Fail(e2e_exit_code::SetupFailed);
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
						Fail(e2e_exit_code::SetupFailed);
						return;
					}

					const RealmData* chosenRealm = nullptr;
					if (!m_config.realmName.empty())
					{
						const auto it = std::find_if(realms.begin(), realms.end(), [this](const RealmData& realm)
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
						ELOG("Realm '" << m_config.realmName << "' not found in realm list.");
						Fail(e2e_exit_code::SetupFailed);
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
						Fail(e2e_exit_code::SetupFailed);
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
							Fail(e2e_exit_code::SetupFailed);
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
							Fail(e2e_exit_code::SetupFailed);
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
						Fail(e2e_exit_code::SetupFailed);
						return;
					}
				});

			m_realm->CharacterCreated.connect([this](game::CharCreateResult result)
				{
					if (result != game::char_create_result::Unknown)
					{
						ELOG("Character creation failed with result " << static_cast<int32>(result));
						Fail(e2e_exit_code::SetupFailed);
						return;
					}

					ILOG("Character \"" << m_config.characterName << "\" created.");
					m_realm->RequestCharEnum();
				});

			m_realm->EnterWorldFailed.connect([this](game::player_login_response::Type reason)
				{
					ELOG("Enter world failed, reason code " << static_cast<int32>(reason));
					Fail(e2e_exit_code::SetupFailed);
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

					m_context->SetWorldReady(true);
					m_worldReady = true;
				});

			m_realm->Disconnected.connect([this]()
				{
					if (m_shuttingDown)
					{
						return;
					}

					ELOG("Realm connection lost.");
					Fail(e2e_exit_code::Disconnected);
				});
		}

	private:
		BotConfig m_config;
		asio::io_service m_io;
		std::shared_ptr<BotLoginConnector> m_login;
		std::shared_ptr<BotRealmConnector> m_realm;
		std::shared_ptr<BotNavService> m_navService;
		std::shared_ptr<BotContext> m_context;
		bool m_stopRequested { false };
		bool m_worldReady { false };
		bool m_realmConnectionAttempted { false };
		bool m_shuttingDown { false };
		e2e_exit_code::Type m_exitCode { e2e_exit_code::Success };
	};
}

int main(int argc, char** argv)
{
	using namespace mmo;

	InitializeLogging();

	cxxopts::Options options("e2e_client", "Headless scenario test client for the mmo project.");
	options.add_options()
		("c,config", "Path to e2e client config JSON (written by tools/e2e/e2e_up.ps1)",
			cxxopts::value<std::string>()->default_value("e2e/runtime/e2e_client.json"))
		("character", "Character name override", cxxopts::value<std::string>())
		("smoke", "Connect, enter world and stay online for --smoke-seconds, then exit")
		("smoke-seconds", "Duration of the smoke test in seconds", cxxopts::value<uint32>()->default_value("60"))
		("connect-timeout", "Timeout in seconds for reaching the world", cxxopts::value<uint32>()->default_value("60"))
		("h,help", "Show help");

	std::string configPath;
	std::string characterOverride;
	bool smokeMode = false;
	uint32 smokeSeconds = 60;
	uint32 connectTimeout = 60;

	try
	{
		const auto results = options.parse(argc, argv);
		if (results.count("help") > 0)
		{
			std::cout << options.help() << '\n';
			return e2e_exit_code::Success;
		}

		configPath = results["config"].as<std::string>();
		smokeMode = results.count("smoke") > 0;
		smokeSeconds = results["smoke-seconds"].as<uint32>();
		connectTimeout = results["connect-timeout"].as<uint32>();
		if (results.count("character") > 0)
		{
			characterOverride = results["character"].as<std::string>();
		}
	}
	catch (const cxxopts::OptionException& e)
	{
		std::cerr << "Failed to parse command line: " << e.what() << '\n';
		std::cerr << options.help() << '\n';
		return e2e_exit_code::SetupFailed;
	}

	// Defaults tuned for the e2e stack; everything can be overridden via the config file.
	BotConfig config;
	config.loginHost = "127.0.0.1";
	// Character names must be 3-12 letters (no digits/symbols - realm-side validation).
	config.characterName = "Smoke";
	config.createCharacter = true;
	config.race = 0;
	config.characterClass = 1;
	config.gender = 0;

	if (!LoadE2eConfig(configPath, config))
	{
		return e2e_exit_code::SetupFailed;
	}

	if (!characterOverride.empty())
	{
		config.characterName = characterOverride;
	}

	E2eSession session(std::move(config));
	if (!session.Start())
	{
		return e2e_exit_code::SetupFailed;
	}

	e2e_exit_code::Type result = session.WaitForWorld(connectTimeout);
	if (result == e2e_exit_code::Success && smokeMode)
	{
		result = session.RunSmoke(smokeSeconds);
	}

	session.Shutdown();
	return result;
}
