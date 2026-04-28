// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_login_connector.h"
#include "bot_realm_connector.h"
#include "bot_console_prompt.h"
#include "bot_context.h"
#include "bot_profile.h"
#include "bot_profiles.h"
#include "bot_startup_selection.h"
#include "bot_startup_types.h"
#include "bot_unit_watcher.h"

#include "base/clock.h"
#include "base/typedefs.h"
#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "asio/io_service.hpp"

#include "nlohmann/json.hpp"
#include <cxxopts/cxxopts.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

using namespace std::chrono_literals;

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace mmo
{
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

	struct StartupCliOptions
	{
		fs::path configPath { "bot_config.json" };
		std::string profileSelector;
		bool profileProvided { false };
		std::string characterSelector;
		bool characterProvided { false };
	};

	enum class StartupProfileSelectionExit
	{
		Success,
		DisplayedHelp,
		Error,
	};

	std::string TrimCopy(std::string_view value)
	{
		size_t start = 0;
		while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
		{
			++start;
		}

		size_t end = value.size();
		while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
		{
			--end;
		}

		return std::string(value.substr(start, end - start));
	}

	std::string DescribeSelector(std::string_view selector)
	{
		const std::string trimmed = TrimCopy(selector);
		return trimmed.empty() ? "<unspecified>" : trimmed;
	}

	StartupProfileSelectionExit ParseStartupOptions(int argc, char** argv, StartupCliOptions& outOptions)
	{
		cxxopts::Options options("mmo_bot", "Bot client for the mmo project.");
		options.add_options()
			("c,config", "Path to bot config JSON", cxxopts::value<std::string>()->default_value(outOptions.configPath.string()))
			("profile", "Startup profile key override", cxxopts::value<std::string>())
			("character", "Character name override", cxxopts::value<std::string>())
			("h,help", "Show help");

		try
		{
			auto results = options.parse(argc, argv);
			if (results.count("help") > 0)
			{
				std::cout << options.help() << '\n';
				return StartupProfileSelectionExit::DisplayedHelp;
			}

			outOptions.configPath = results["config"].as<std::string>();
			if (results.count("profile") > 0)
			{
				outOptions.profileProvided = true;
				outOptions.profileSelector = TrimCopy(results["profile"].as<std::string>());
				if (outOptions.profileSelector.empty())
				{
					std::cerr << "Invalid --profile value: selector must not be empty." << '\n';
					std::cerr << options.help() << '\n';
					return StartupProfileSelectionExit::Error;
				}
			}

			if (results.count("character") > 0)
			{
				outOptions.characterProvided = true;
				outOptions.characterSelector = TrimCopy(results["character"].as<std::string>());
				if (outOptions.characterSelector.empty())
				{
					std::cerr << "Invalid --character value: selector must not be empty." << '\n';
					std::cerr << options.help() << '\n';
					return StartupProfileSelectionExit::Error;
				}
			}
		}
		catch (const cxxopts::OptionException& e)
		{
			std::cerr << "Failed to parse command line: " << e.what() << '\n';
			std::cerr << options.help() << '\n';
			return StartupProfileSelectionExit::Error;
		}

		return StartupProfileSelectionExit::Success;
	}

	void LogProfileResolution(const StartupProfileResolution& resolution, size_t availableCount)
	{
		switch (resolution.kind)
		{
		case StartupProfileResolutionKind::Resolved:
			ILOG("startup phase=profile_resolution outcome=resolved selector="
				<< DescribeSelector(resolution.selector)
				<< " resolved_profile=" << resolution.resolvedKey
				<< " available_count=" << availableCount);
			break;
		case StartupProfileResolutionKind::NeedsPrompt:
			ILOG("startup phase=profile_resolution outcome=prompt_required selector="
				<< DescribeSelector(resolution.selector)
				<< " available_count=" << resolution.candidateIndices.size());
			break;
		case StartupProfileResolutionKind::Invalid:
			ELOG("startup phase=profile_resolution outcome=invalid selector="
				<< DescribeSelector(resolution.selector)
				<< " available_count=" << availableCount);
			break;
		}
	}

	std::vector<std::string> BuildPromptOptions(
		const std::vector<StartupProfileEntry>& catalog,
		const std::vector<size_t>& indices)
	{
		std::vector<std::string> options;
		options.reserve(indices.size());
		for (const size_t index : indices)
		{
			if (index < catalog.size())
			{
				options.push_back(catalog[index].key);
			}
		}
		return options;
	}

	std::optional<size_t> PromptForProfileSelection(
		const std::vector<StartupProfileEntry>& catalog,
		const StartupProfileResolution& resolution)
	{
		const std::vector<std::string> promptOptions = BuildPromptOptions(catalog, resolution.candidateIndices);
		const BotConsolePromptResult promptResult = PromptForSelection(
			"profile",
			promptOptions,
			std::cin,
			std::cout,
			IsConsoleInputInteractive());

		switch (promptResult.kind)
		{
		case BotConsolePromptResultKind::Selected:
			if (promptResult.selectedIndex.has_value() && *promptResult.selectedIndex < resolution.candidateIndices.size())
			{
				const size_t resolvedIndex = resolution.candidateIndices[*promptResult.selectedIndex];
				ILOG("startup phase=profile_resolution outcome=prompt_selected selector="
					<< catalog[resolvedIndex].key
					<< " available_count=" << resolution.candidateIndices.size());
				return resolvedIndex;
			}
			break;
		case BotConsolePromptResultKind::NonInteractive:
			ELOG("startup phase=profile_resolution outcome=non_interactive_refusal selector="
				<< DescribeSelector(resolution.selector)
				<< " available_count=" << resolution.candidateIndices.size());
			break;
		case BotConsolePromptResultKind::Aborted:
			ELOG("startup phase=profile_resolution outcome=prompt_aborted selector="
				<< DescribeSelector(resolution.selector)
				<< " available_count=" << resolution.candidateIndices.size());
			break;
		}

		return std::nullopt;
	}

	void LogCharacterResolution(const StartupCharacterResolution& resolution, size_t availableCount)
	{
		switch (resolution.kind)
		{
		case StartupCharacterResolutionKind::Resolved:
			ILOG("startup phase=character_resolution outcome=resolved selector="
				<< DescribeSelector(resolution.selector)
				<< " resolved_character=" << resolution.resolvedName
				<< " available_count=" << availableCount);
			break;
		case StartupCharacterResolutionKind::NeedsPrompt:
			ILOG("startup phase=character_resolution outcome=prompt_required selector="
				<< DescribeSelector(resolution.selector)
				<< " available_count=" << resolution.candidateIndices.size());
			break;
		case StartupCharacterResolutionKind::CreateCharacter:
			ILOG("startup phase=character_resolution outcome=create_requested selector="
				<< DescribeSelector(resolution.selector)
				<< " available_count=" << availableCount);
			break;
		case StartupCharacterResolutionKind::NotFound:
			ELOG("startup phase=character_resolution outcome=not_found selector="
				<< DescribeSelector(resolution.selector)
				<< " available_count=" << availableCount);
			break;
		}
	}

	std::string DescribeCharacterOption(const CharacterView& view)
	{
		std::ostringstream label;
		const std::string name = TrimCopy(view.GetName());
		label << (name.empty() ? std::string("<unnamed>") : name)
			<< " (level " << static_cast<unsigned>(view.GetLevel())
			<< ", race " << view.GetRaceId()
			<< ", class " << view.GetClassId();
		if (view.IsDead())
		{
			label << ", dead";
		}
		label << ')';
		return label.str();
	}

	std::vector<std::string> BuildCharacterPromptOptions(
		const std::vector<CharacterView>& catalog,
		const std::vector<size_t>& indices)
	{
		std::vector<std::string> options;
		options.reserve(indices.size());
		for (const size_t index : indices)
		{
			if (index < catalog.size())
			{
				options.push_back(DescribeCharacterOption(catalog[index]));
			}
		}
		return options;
	}

	std::optional<size_t> PromptForCharacterSelection(
		const std::vector<CharacterView>& catalog,
		const StartupCharacterResolution& resolution)
	{
		const std::vector<std::string> promptOptions = BuildCharacterPromptOptions(catalog, resolution.candidateIndices);
		const BotConsolePromptResult promptResult = PromptForSelection(
			"character",
			promptOptions,
			std::cin,
			std::cout,
			IsConsoleInputInteractive());

		switch (promptResult.kind)
		{
		case BotConsolePromptResultKind::Selected:
			if (promptResult.selectedIndex.has_value() && *promptResult.selectedIndex < resolution.candidateIndices.size())
			{
				const size_t resolvedIndex = resolution.candidateIndices[*promptResult.selectedIndex];
				ILOG("startup phase=character_resolution outcome=prompt_selected selector="
					<< catalog[resolvedIndex].GetName()
					<< " available_count=" << resolution.candidateIndices.size());
				return resolvedIndex;
			}
			break;
		case BotConsolePromptResultKind::NonInteractive:
			ELOG("startup phase=character_resolution outcome=non_interactive_refusal selector="
				<< DescribeSelector(resolution.selector)
				<< " available_count=" << resolution.candidateIndices.size());
			break;
		case BotConsolePromptResultKind::Aborted:
			ELOG("startup phase=character_resolution outcome=prompt_aborted selector="
				<< DescribeSelector(resolution.selector)
				<< " available_count=" << resolution.candidateIndices.size());
			break;
		}

		return std::nullopt;
	}

	bool ResolveConfiguredProfile(
		BotConfig& config,
		const StartupCliOptions& cliOptions,
		BotProfilePtr& outProfile)
	{
		const std::vector<StartupProfileEntry> catalog = GetStartupProfileCatalog();
		StartupProfileResolution resolution = ResolveStartupProfileSelection(
			cliOptions.profileProvided ? cliOptions.profileSelector : std::string_view{},
			config.profileName,
			catalog);
		LogProfileResolution(resolution, catalog.size());

		if (resolution.kind == StartupProfileResolutionKind::NeedsPrompt)
		{
			const std::optional<size_t> promptedIndex = PromptForProfileSelection(catalog, resolution);
			if (!promptedIndex.has_value())
			{
				return false;
			}

			resolution.kind = StartupProfileResolutionKind::Resolved;
			resolution.resolvedIndex = *promptedIndex;
			resolution.resolvedKey = catalog[*promptedIndex].key;
		}
		else if (resolution.kind == StartupProfileResolutionKind::Invalid)
		{
			std::cerr << "Valid profiles:";
			for (const auto& entry : catalog)
			{
				std::cerr << ' ' << entry.key;
			}
			std::cerr << '\n';
			return false;
		}

		if (!resolution.resolvedIndex.has_value())
		{
			ELOG("startup phase=profile_resolution outcome=missing_resolved_profile selector="
				<< DescribeSelector(resolution.selector)
				<< " available_count=" << catalog.size());
			return false;
		}

		config.profileName = resolution.resolvedKey;
		outProfile = CreateBotProfile(config.profileName, config);
		if (!outProfile)
		{
			ELOG("startup phase=profile_resolution outcome=unknown_profile_factory selector="
				<< config.profileName
				<< " available_count=" << catalog.size());
			return false;
		}

		return true;
	}

	void ApplyCliOverrides(BotConfig& config, const StartupCliOptions& cliOptions)
	{
		if (cliOptions.characterProvided)
		{
			config.characterName = cliOptions.characterSelector;
		}
	}

	class BotSession
	{
	public:
		BotSession(BotConfig config, BotProfilePtr profile)
			: m_config(std::move(config))
			, m_io()
			, m_login(std::make_shared<BotLoginConnector>(m_io, m_config.loginHost, m_config.loginPort))
			, m_realm(std::make_shared<BotRealmConnector>(m_io))
			, m_context(std::make_shared<BotContext>(m_realm, m_config))
			, m_profile(std::move(profile))
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
					StartupCharacterResolution resolution = ResolveStartupCharacterSelection(
						std::string_view{},
						m_config.characterName,
						m_config.createCharacter,
						views);
					LogCharacterResolution(resolution, views.size());

					switch (resolution.kind)
					{
					case StartupCharacterResolutionKind::Resolved:
						if (!resolution.resolvedIndex.has_value() || *resolution.resolvedIndex >= views.size())
						{
							ELOG("startup phase=character_resolution outcome=missing_resolved_character selector="
								<< DescribeSelector(resolution.selector)
								<< " available_count=" << views.size());
							m_stopRequested = true;
							return;
						}

						m_config.characterName = views[*resolution.resolvedIndex].GetName();
						ILOG("Using existing character \"" << m_config.characterName << "\".");
						m_realm->EnterWorld(views[*resolution.resolvedIndex]);
						return;
					case StartupCharacterResolutionKind::NeedsPrompt:
						if (const std::optional<size_t> promptedIndex = PromptForCharacterSelection(views, resolution); promptedIndex.has_value())
						{
							m_config.characterName = views[*promptedIndex].GetName();
							ILOG("Using selected character \"" << m_config.characterName << "\".");
							m_realm->EnterWorld(views[*promptedIndex]);
							return;
						}

						m_stopRequested = true;
						return;
					case StartupCharacterResolutionKind::CreateCharacter:
					{
						if (resolution.selector.empty())
						{
							ELOG("startup phase=character_create outcome=missing_selector selector="
								<< DescribeSelector(resolution.selector)
								<< " available_count=" << views.size());
							m_stopRequested = true;
							return;
						}

						m_config.characterName = resolution.selector;
						ILOG("startup phase=character_create outcome=requested selector="
							<< DescribeSelector(resolution.selector)
							<< " available_count=" << views.size());
						AvatarConfiguration avatarConfig;
						m_realm->CreateCharacter(resolution.selector, m_config.race, m_config.characterClass, m_config.gender, avatarConfig);
						return;
					}
					case StartupCharacterResolutionKind::NotFound:
						m_stopRequested = true;
						return;
					}
				});

			m_realm->CharacterCreated.connect([this](game::CharCreateResult result)
				{
					if (result != game::char_create_result::Unknown)
					{
						ELOG("startup phase=character_create outcome=failed selector="
							<< DescribeSelector(m_config.characterName)
							<< " result=" << static_cast<int32>(result));
						m_stopRequested = true;
						return;
					}

					ILOG("startup phase=character_create outcome=created selector="
						<< DescribeSelector(m_config.characterName)
						<< " available_count=" << m_realm->GetCharacterViews().size());
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

			// Combat signals
			m_realm->AttackHit.connect([this](uint64 attackerGuid, uint64 victimGuid, uint32 damage, uint32 hitInfo, uint32 victimState)
				{
					if (!m_profile || !m_profileActivated)
					{
						return;
					}

					// Check if we are the attacker
					if (attackerGuid == m_realm->GetSelectedGuid())
					{
						m_profile->OnAttackSwing(*m_context, victimGuid, damage, hitInfo, victimState);
					}
				});

			m_realm->AttackSwingError.connect([this](AttackSwingEvent error)
				{
					if (!m_profile || !m_profileActivated)
					{
						return;
					}

					m_profile->OnAttackSwingError(*m_context, error);
				});

			m_realm->DamageReceived.connect([this](uint64 targetGuid, uint32 damage, uint8 flags)
				{
					if (!m_profile || !m_profileActivated)
					{
						return;
					}

					// Check if we are the target (we took damage)
					if (targetGuid == m_realm->GetSelectedGuid())
					{
						m_profile->OnDamaged(*m_context, damage, flags);
					}
					else
					{
						// We dealt damage to someone
						bool isCrit = (flags & 1) != 0; // damage_flags::Crit = 1
						m_profile->OnDamagedUnit(*m_context, targetGuid, damage, isCrit);
					}
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

int main(int argc, char** argv)
{
	using namespace mmo;

	InitializeLogging();

	StartupCliOptions cliOptions;
	switch (ParseStartupOptions(argc, argv, cliOptions))
	{
	case StartupProfileSelectionExit::DisplayedHelp:
		return 0;
	case StartupProfileSelectionExit::Error:
		return 1;
	case StartupProfileSelectionExit::Success:
		break;
	}

	ILOG("startup phase=config_load config_path=" << cliOptions.configPath);

	BotConfig config;
	if (!LoadConfig(cliOptions.configPath, config))
	{
		return 1;
	}

	ApplyCliOverrides(config, cliOptions);

	BotProfilePtr profile;
	if (!ResolveConfiguredProfile(config, cliOptions, profile))
	{
		return 1;
	}

	BotSession session(std::move(config), std::move(profile));
	if (!session.Start())
	{
		return 1;
	}

	session.Run();
	return 0;
}
