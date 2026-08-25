#include "client_application.h"

#include "assets/asset_registry.h"
#include "base/task_system.h"
#include "base/timer_queue.h"
#include "char_creation/char_create_info.h"
#include "char_creation/char_select.h"
#include "client_context.h"
#include "client_runtime.h"
#include "client_ui_runtime.h"
#include "client_data/project.h"
#include "console/console.h"
#include "cursor.h"
#include "data/client_cache.h"
#include "discord.h"
#include "event_loop.h"
#include "game/game_time_component.h"
#include "game_script.h"
#include "game_states/game_state_mgr.h"
#include "game_states/login_state.h"
#include "game_states/world_state.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "systems/action_bar.h"
#include "systems/cooldown_manager.h"
#include "systems/friend_client.h"
#include "systems/channel_client.h"
#include "systems/guild_client.h"
#include "systems/inventory_client.h"
#include "systems/loot_client.h"
#include "systems/party_info.h"
#include "systems/quest_client.h"
#include "systems/spell_cast.h"
#include "systems/talent_client.h"
#include "systems/trade_client.h"
#include "systems/trainer_client.h"
#include "systems/vendor_client.h"
#include "systems/bank_client.h"
#include "systems/mail_client.h"
#include "ui/minimap.h"
#include "frame_ui/frame_mgr.h"
#include "startup_error.h"

#include <filesystem>

#ifdef _WIN32
#include "fmod_audio/fmod_audio.h"
#else
#include "null_audio/null_audio.h"
#endif

#include "audio_settings.h"
#include "game_client/game_unit_c.h"
#include "game_client/sound_entry_player.h"
#include "systems/cast_error_voice.h"
#include "systems/unit_gossip_voice.h"

namespace mmo
{
	extern Cursor g_cursor;

	/// @copydoc ClientApplication::Start
	bool ClientApplication::Start()
	{
		if (m_started)
		{
			return true;
		}

		auto& context = GetClientContext();
		LoginConnector* loginConnector = nullptr;
		RealmConnector* realmConnector = nullptr;

		if (!InitializeCore(context) ||
			!InitializeRuntimeServices(context, loginConnector, realmConnector) ||
			!LoadProjectAndCache(context, *realmConnector))
		{
			AbortStart(context);
			return false;
		}

		InitializeGameplaySystems(context, *realmConnector);
		InitializeStatesAndScripts(context, *loginConnector, *realmConnector);
		if (!InitializeUiAndEnterState(context))
		{
			AbortStart(context);
			return false;
		}

		m_started = true;
		return true;
	}

	/// @copydoc ClientApplication::Stop
	void ClientApplication::Stop()
	{
		if (!m_started)
		{
			return;
		}

		auto& context = GetClientContext();
		ShutdownSystems(context);
		ResetContext(context);

		m_started = false;
	}

	/// @copydoc ClientApplication::InitializeCore
	bool ClientApplication::InitializeCore(ClientContext& context)
	{
		context.project = std::make_unique<proto_client::Project>();
		context.gameTime = std::make_unique<GameTimeComponent>();
		context.timerQueue = std::make_unique<TimerQueue>(context.timerService);
		context.uiRuntime = std::make_unique<ClientUiRuntime>();

		std::error_code error;
		const auto currentPath = std::filesystem::current_path(error);
		if (error)
		{
			ELOG("Could not obtain working directory: " << error);
			return false;
		}

		std::filesystem::create_directories(currentPath / "Logs");
		std::filesystem::create_directories(currentPath / "Config");

		context.logFile.open((currentPath / "Logs" / "Client.log").string().c_str(), std::ios::out);
		if (context.logFile)
		{
			context.logConnection = g_DefaultLog.signal().connect(
				[](const LogEntry& entry)
				{
					printLogEntry(GetClientContext().logFile, entry, g_DefaultFileLogOptions);
				});
		}

		// Start the worker pool for fork-join parallel work (particles, animation, ...).
		// Must happen on the main thread before any system that uses ParallelFor.
		TaskSystem::Get().Initialize();

		EventLoop::Initialize();
		if (!Console::Initialize(ClientConfigFilePath))
		{
			return false;
		}

		return true;
	}

	/// @copydoc ClientApplication::InitializeRuntimeServices
	bool ClientApplication::InitializeRuntimeServices(ClientContext& context, LoginConnector*& outLoginConnector, RealmConnector*& outRealmConnector)
	{
		context.runtime = std::make_unique<ClientRuntime>();
		context.runtime->Initialize();

#ifdef _WIN32
		context.audio = std::make_unique<FMODAudio>();
#else
		context.audio = std::make_unique<NullAudio>();
#endif
		context.audio->Create();
		context.audioSettings = std::make_unique<AudioSettings>(*context.audio);

		context.timerConnection = EventLoop::Idle.connect([](float, const GameTime&)
			{
				auto& localContext = GetClientContext();
				if (localContext.runtime)
				{
					localContext.runtime->PollNetwork();
				}

				localContext.timerService.poll_one();
			});

		if (!context.runtime->IsInitialized())
		{
			return false;
		}

		outLoginConnector = &context.runtime->GetLoginConnector();
		outRealmConnector = &context.runtime->GetRealmConnector();
		return true;
	}

	/// @copydoc ClientApplication::LoadProjectAndCache
	bool ClientApplication::LoadProjectAndCache(ClientContext& context, RealmConnector& realmConnector)
	{
		if (!context.project->load("ClientDB"))
		{
			// Same reasoning as the data path checks in Console::Initialize: a damaged or
			// incomplete install is nothing the player can report usefully, but it is something
			// they can fix, so tell them instead of exiting without a word.
			ShowStartupError("The game data could not be loaded. The installation appears to be incomplete or damaged.");
			return false;
		}

		context.soundEntryPlayer = std::make_unique<SoundEntryPlayer>(*context.audio, context.project->sounds);
		CastErrorVoice::Get().Initialize(context.soundEntryPlayer.get(), &context.project->races);
		UnitGossipVoice::Get().Initialize(context.soundEntryPlayer.get(), &context.project->models, context.timerQueue.get());
		GameUnitC::SetSoundEntryPlayer(context.soundEntryPlayer.get());

		context.clientCache = std::make_unique<ClientCache>(realmConnector);
		if (!context.clientCache->Load())
		{
			// Deleting the config file would not help here, so say what actually does: the cache
			// lives in the game data directory and the client rebuilds it from the server.
			ShowStartupError("The client cache could not be loaded.", "Cannot start the game",
				"Deleting the Cache folder in your game data directory usually fixes this. "
				"The game rebuilds it while you play.");
			return false;
		}

		return true;
	}

	/// @copydoc ClientApplication::InitializeGameplaySystems
	void ClientApplication::InitializeGameplaySystems(ClientContext& context, RealmConnector& realmConnector)
	{
		context.discord = std::make_unique<Discord>();
		context.discord->Initialize();
		context.minimap = std::make_unique<Minimap>(256);

		context.charCreateInfo = std::make_unique<CharCreateInfo>(*context.project, realmConnector);
		context.charSelect = std::make_unique<CharSelect>(*context.project, realmConnector);
		context.lootClient = std::make_unique<LootClient>(realmConnector, context.clientCache->GetItemCache());
		context.vendorClient = std::make_unique<VendorClient>(realmConnector, context.clientCache->GetItemCache());
		context.bankClient = std::make_unique<BankClient>(realmConnector);
		context.mailClient = std::make_unique<MailClient>(realmConnector, context.clientCache->GetItemCache());
		context.trainerClient = std::make_unique<TrainerClient>(realmConnector, context.project->spells);
		context.inventoryClient = std::make_unique<InventoryClient>(realmConnector);
		context.uiRuntime->LoadLocalization();
		context.questClient = std::make_unique<QuestClient>(realmConnector, context.clientCache->GetQuestCache(), context.project->spells, context.project->emotes, context.clientCache->GetItemCache(), context.clientCache->GetCreatureCache(), context.clientCache->GetObjectCache(), context.uiRuntime->GetLocalization());
		context.partyInfo = std::make_unique<PartyInfo>(realmConnector, context.clientCache->GetNameCache());
		context.guildClient = std::make_unique<GuildClient>(realmConnector, context.clientCache->GetGuildCache(), context.project->races, context.project->classes);
		context.friendClient = std::make_unique<FriendClient>(realmConnector, context.project->races, context.project->classes);
		context.channelClient = std::make_unique<ChannelClient>(realmConnector);
		context.spellCast = std::make_unique<SpellCast>(realmConnector, context.project->spells, context.project->ranges);
		context.cooldownManager = std::make_unique<CooldownManager>(context.project->spells);
		context.actionBar = std::make_unique<ActionBar>(realmConnector, context.project->spells, context.project->emotes, context.clientCache->GetItemCache(), *context.spellCast);
		context.talentClient = std::make_unique<TalentClient>(context.project->talentTabs, context.project->talents, context.project->spells, realmConnector);
		context.tradeClient = std::make_unique<TradeClient>(realmConnector);
	}

	/// @copydoc ClientApplication::InitializeStatesAndScripts
	void ClientApplication::InitializeStatesAndScripts(ClientContext& context, LoginConnector& loginConnector, RealmConnector& realmConnector)
	{
		GameStateMgr& gameStateMgr = GameStateMgr::Get();
		const auto loginState = std::make_shared<LoginState>(gameStateMgr, loginConnector, realmConnector, *context.timerQueue, *context.audio, *context.soundEntryPlayer, *context.discord);
		gameStateMgr.AddGameState(loginState);

		const auto worldState = std::make_shared<WorldState>(gameStateMgr, realmConnector, *context.project, *context.timerQueue, *context.lootClient, *context.vendorClient,
			*context.actionBar, *context.spellCast, *context.cooldownManager, *context.trainerClient, *context.questClient, *context.audio, *context.soundEntryPlayer, *context.partyInfo, *context.charSelect, *context.guildClient, *context.friendClient, *context.clientCache, *context.discord, *context.gameTime, *context.talentClient,
			*context.minimap, *context.inventoryClient, *context.tradeClient, *context.channelClient, *context.bankClient, *context.mailClient);
		gameStateMgr.AddGameState(worldState);

		context.gameScript = std::make_unique<GameScript>(loginConnector, realmConnector, *context.lootClient, *context.vendorClient, loginState, *context.project, *context.actionBar, *context.spellCast, *context.cooldownManager, *context.trainerClient, *context.questClient, *context.audio, *context.partyInfo, *context.charCreateInfo, *context.charSelect, *context.guildClient, *context.friendClient, *context.gameTime, *context.talentClient, *context.clientCache, *context.tradeClient, *context.channelClient);
		context.minimap->RegisterScriptFunctions(&context.gameScript->GetLuaState());
		context.bankClient->RegisterScriptFunctions(&context.gameScript->GetLuaState());
		context.mailClient->RegisterScriptFunctions(&context.gameScript->GetLuaState());
	}

	/// @copydoc ClientApplication::InitializeUiAndEnterState
	bool ClientApplication::InitializeUiAndEnterState(ClientContext& context)
	{
		if (!context.uiRuntime->Initialize(*context.gameScript, *context.minimap, g_cursor))
		{
			return false;
		}

		GameStateMgr::Get().SetGameState(LoginState::Name);
		Console::ExecuteCommand("run Config/RunOnce.cfg");

		const auto window = GraphicsDevice::Get().GetAutoCreatedWindow();
		if (window)
		{
			FrameManager::Get().NotifyScreenSizeChanged(window->GetWidth(), window->GetHeight());
		}

		return true;
	}

	/// @copydoc ClientApplication::AbortStart
	void ClientApplication::AbortStart(ClientContext& context)
	{
		context.timerConnection.disconnect();
		GameStateMgr::Get().RemoveAllGameStates();

		// The shutdown helpers all guard on the pointers they touch, so they cope with a stage
		// that never ran. Two things from the normal path are deliberately left out:
		//
		//  - the client cache is not saved. A start that failed may have failed *while* loading
		//    the cache, and writing that half-read state back would destroy the good file.
		//  - Console::Destroy() is not called. Console::Initialize can bail out before the screen
		//    and its layers exist, and Destroy() would hand a default constructed layer iterator
		//    to Screen::RemoveLayer.
		ShutdownGameplaySystems(context);
		ShutdownUiSystems(context);

		if (context.runtime)
		{
			context.runtime->Shutdown();
			context.runtime.reset();
		}

		// Both of these are no-ops when the stage that would have set them up never ran.
		EventLoop::Destroy();
		AssetRegistry::Destroy();

		TaskSystem::Get().Shutdown();

		g_DefaultLog.FlushBuffered();
		context.logConnection.disconnect();
		context.logFile.close();
		context.timerService.stop();
		context.timerService.reset();

		ResetContext(context);
	}

	/// @copydoc ClientApplication::ShutdownSystems
	void ClientApplication::ShutdownSystems(ClientContext& context)
	{
		context.timerConnection.disconnect();
		GameStateMgr::Get().RemoveAllGameStates();
		ShutdownGameplaySystems(context);
		ShutdownUiSystems(context);
		ShutdownRuntimeServices(context);
		ShutdownCoreServices(context);
	}

	/// @copydoc ClientApplication::ShutdownGameplaySystems
	void ClientApplication::ShutdownGameplaySystems(ClientContext& context)
	{
		if (context.lootClient) context.lootClient->Shutdown();
		if (context.vendorClient) context.vendorClient->Shutdown();
		if (context.bankClient) context.bankClient->Shutdown();
		if (context.mailClient) context.mailClient->Shutdown();
		if (context.trainerClient) context.trainerClient->Shutdown();
		if (context.inventoryClient) context.inventoryClient->Shutdown();
		if (context.questClient) context.questClient->Shutdown();
		if (context.partyInfo) context.partyInfo->Shutdown();
		if (context.guildClient) context.guildClient->Shutdown();
		if (context.friendClient) context.friendClient->Shutdown();

		context.vendorClient.reset();
		context.bankClient.reset();
		context.mailClient.reset();
		context.lootClient.reset();
		context.trainerClient.reset();
		context.inventoryClient.reset();
	}

	/// @copydoc ClientApplication::ShutdownUiSystems
	void ClientApplication::ShutdownUiSystems(ClientContext& context)
	{
		if (context.uiRuntime)
		{
			context.uiRuntime->Shutdown();
		}

		context.gameScript.reset();
		context.minimap.reset();
	}

	/// @copydoc ClientApplication::ShutdownRuntimeServices
	void ClientApplication::ShutdownRuntimeServices(ClientContext& context)
	{
		if (context.runtime)
		{
			context.runtime->Shutdown();
			context.runtime.reset();
		}

		if (context.clientCache)
		{
			context.clientCache->Save();
		}
	}

	/// @copydoc ClientApplication::ShutdownCoreServices
	void ClientApplication::ShutdownCoreServices(ClientContext& context)
	{
		Console::Destroy();
		EventLoop::Destroy();
		AssetRegistry::Destroy();

		// Stop the worker pool after all systems that might still hold parallel work are gone.
		TaskSystem::Get().Shutdown();

		// Emit anything background threads logged after the last frame's flush, while the
		// log file connection is still alive.
		g_DefaultLog.FlushBuffered();

		context.logConnection.disconnect();
		context.logFile.close();
		context.timerService.stop();
		context.timerService.reset();
	}

	/// @copydoc ClientApplication::ResetContext
	void ClientApplication::ResetContext(ClientContext& context)
	{
		context.talentClient.reset();
		context.tradeClient.reset();
		context.actionBar.reset();
		context.spellCast.reset();
		context.cooldownManager.reset();
		context.questClient.reset();
		context.partyInfo.reset();
		context.guildClient.reset();
		context.friendClient.reset();
		context.channelClient.reset();
		context.charCreateInfo.reset();
		context.charSelect.reset();
		context.discord.reset();
		context.clientCache.reset();
		context.timerQueue.reset();
		context.gameTime.reset();

		// The sound entry player holds references to the audio system and to the project's sound
		// data, and three statics hold a raw pointer to it. Drop all of that before the things it
		// points at go away, or a later shutdown step walks into freed memory.
		GameUnitC::SetSoundEntryPlayer(nullptr);
		CastErrorVoice::Get().Initialize(nullptr, nullptr);
		UnitGossipVoice::Get().Initialize(nullptr, nullptr, nullptr);
		context.soundEntryPlayer.reset();

		context.project.reset();
		context.uiRuntime.reset();

		// Owns cvar subscriptions that apply volume changes to the audio system, so it has to go
		// first (see the note on the member declaration).
		context.audioSettings.reset();
		context.audio.reset();
	}
}
