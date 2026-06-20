// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shared/audio/audio.h"
#include "mmo_client/char_creation//char_select.h"
#include "mmo_client/data/client_cache.h"
#include "connection.h"
#include "mmo_client/data/db_cache.h"
#include "mmo_client/area_trigger_manager.h"
#include "player_controller.h"
#include "screen.h"
#include "game_states/game_state.h"

#include "base/signal.h"
#include "game/game_time_component.h"
#include "graphics/sky_component.h"
#include "game_client/game_object_c.h"
#include "game_protocol/game_protocol.h"
#include "scene_graph/axis_display.h"
#include "ui/minimap.h"
#include "world_ping_visualizer.h"
#include "scene_graph/scene.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/animation_notify.h"
#include "game_client/projectile_manager.h"

#include "base/id_generator.h"
#include "world_deserializer.h"
#include "client_data/project.h"
#include "frame_ui/frame.h"
#include "game/auto_attack.h"
#include "paging/loaded_page_section.h"
#include "paging/page_loader_listener.h"
#include "paging/page_pov_partitioner.h"
#include "paging/world_page_loader.h"
#include "ui/binding.h"
#include "ui/world_text_frame.h"

#include "game_client/net_client.h"
#include "debug_path_visualizer.h"
#include "scene_graph/foliage.h"

#include <map>
#include <tuple>
#include <vector>

namespace mmo
{
	class ConsoleVar;
}

namespace mmo
{
	class IAudio;
}

namespace mmo
{
	class QuestClient;
	class ActionBar;
	class VendorClient;
	class TimerQueue;

	namespace game
	{
		class IncomingPacket;
	}

	class LoginConnector;
	class RealmConnector;

	class Entity;
	class SceneNode;

	class LootClient;
	class TrainerClient;
	class PartyInfo;
	class GuildClient;
	class FriendClient;
	class ChannelClient;
	class TalentClient;
	class TradeClient;
	class Discord;
	class InventoryClient;
	class CooldownManager;
	class MaterialInterface;

	/// This class represents the initial game state where the player is asked to enter
	/// his credentials in order to authenticate.
	class WorldState final
		: public GameState,
		  public NetClient,
		  public IPageLoaderListener
	{
	public:
		/// @brief Creates a new instance of the WorldState class and initializes it.
		/// @param gameStateManager The game state manager that this state belongs to.
		/// @param realmConnector The connector which manages the connection to the realm server.
		/// @param project
		explicit WorldState(
			GameStateMgr &gameStateManager,
			RealmConnector &realmConnector,
			const proto_client::Project &project,
			TimerQueue &timers,
			LootClient &lootClient,
			VendorClient &vendorClient,
			ActionBar &actionBar,
			SpellCast &spellCast,
			CooldownManager &cooldownManager,
			TrainerClient &trainerClient,
			QuestClient &questClient,
			IAudio &audio,
			PartyInfo &partyInfo,
			CharSelect &charSelect,
			GuildClient &guildClient,
			FriendClient &friendClient,
			ICacheProvider &cache,
			Discord &discord,
			GameTimeComponent &gameTime,
			TalentClient &talentClient,
			Minimap &minimap,
			InventoryClient &inventoryClient,
			TradeClient &tradeClient,
			ChannelClient &channelClient);

	public:
		/// @brief The default name of the world state
		static const std::string Name;

	public:
		/// @copydoc GameState::OnEnter
		void OnEnter() override;

		/// @copydoc GameState::OnLeave
		void OnLeave() override;

		/// @copydoc GameState::GetName
		[[nodiscard]] std::string_view GetName() const override;

	private:
		// Player Mirror Handlers (called when certain field map values of the controlled player character were changed by the server)

		void ReloadUI();

		/// Registers WorldState-specific Lua globals (called from ReloadUI so they survive UI reload).
		void RegisterWorldLuaFunctions();

		void OnTargetSelectionChanged(uint64 monitoredGuid);

		void OnMoneyChanged(uint64 monitoredGuid);

		void OnExperiencePointsChanged(uint64 monitoredGuid);

		void OnLevelChanged(uint64 monitoredGuid);

		void OnQuestLogChanged(uint64 monitoredGuid);

		void OnPlayerPowerChanged(uint64 monitoredGuid);

		void OnPlayerHealthChanged(uint64 monitoredGuid);

		void OnPlayerAttributePointsChanged(uint64 monitoredGuid);

		void OnPlayerStatsChanged(uint64 monitoredGuid);

		void OnDisplayIdChanged(uint64 monitoredGuid);

		void OnCombatModeChanged(uint64 monitoredGuid);

		void OnPlayerGuildChanged(uint64 monitoredGuid);

	private:
		// Selected Target Mirror Handlers (called when certain field map values of the selected target object were changed by the server)

		void OnTargetHealthChanged(uint64 monitoredGuid);

		void OnTargetPowerChanged(uint64 monitoredGuid);

		void OnTargetLevelChanged(uint64 monitoredGuid);

		// Forward declaration for PendingProjectile (defined below)
		struct PendingProjectile;

		/// @brief Spawns a pending projectile and removes it from the pending list.
		void SpawnPendingProjectile(PendingProjectile* pending);

		/// @brief Updates pending projectiles (checks for animation end fallback).
		void UpdatePendingProjectiles();

		void OnRenderShadowsChanged(ConsoleVar &var, const std::string &oldValue);

		void OnShadowBiasChanged(ConsoleVar &var, const std::string &oldValue);

		void OnShadowTextureSizeChanged(ConsoleVar &var, const std::string &oldValue);

		void OnShadowQualityChanged(ConsoleVar &var, const std::string &oldValue);

		void OnShadowTemporalChanged(ConsoleVar &var, const std::string &oldValue);

		void OnDepthPrepassChanged(ConsoleVar &var, const std::string &oldValue);

		void OnFoliageEnabledChanged(ConsoleVar &var, const std::string &oldValue);

		void OnFoliageDensityChanged(ConsoleVar &var, const std::string &oldValue);

		void OnTerrainLodEnabledChanged(ConsoleVar& var, const std::string& oldValue);

		void OnTerrainOcclusionCullingChanged(ConsoleVar& var, const std::string& oldValue);

	private:
		// EventLoop connections

		bool OnMouseDown(MouseButton button, int32 x, int32 y);

		bool OnMouseUp(MouseButton button, int32 x, int32 y);

		bool OnMouseMove(int32 x, int32 y);

		bool OnMouseWheel(int32 delta);

		bool OnKeyDown(int32 key, bool repeat);

		bool OnKeyUp(int32 key);

		void OnIdle(float deltaSeconds, GameTime timestamp);

		/// Called when the screen layer should be painted. Should paint the scene.
		void OnPaint();

		void CheckForZoneUpdate();

	private:
		// Setup stuff

		void SetupWorldScene();

		void SetupFoliage();

		/// @brief Registers procedural foliage layers from the tile materials of a loaded terrain page.
		/// @param pageX The terrain page X index.
		/// @param pageY The terrain page Y index.
		void RegisterPageFoliage(uint32 pageX, uint32 pageY);

		/// @brief Ref-decrements (and removes when unused) the foliage layers a terrain page contributed.
		/// @param pageX The terrain page X index.
		/// @param pageY The terrain page Y index.
		void UnregisterPageFoliage(uint32 pageX, uint32 pageY);

		void SetupPacketHandler();

		void RemovePacketHandler();

	private:
		//

		void OnRealmDisconnected();

		void OnEnterWorldFailed(game::player_login_response::Type error);

		void RegisterGameplayCommands();

		void RemoveGameplayCommands();

	private:
		// Gameplay command callbacks

		void ToggleAxisVisibility() const;

		void ToggleGridVisibility() const;

		void ToggleWireframe() const;

		void WaitForWorldLoading();

	private:
		// Network packet handlers

		PacketParseResult OnUpdateObject(game::IncomingPacket &packet);

		PacketParseResult OnCompressedUpdateObject(game::IncomingPacket &packet);

		/// Parses an object update body (object count + update blocks) from the given reader.
		/// Shared by OnUpdateObject and OnCompressedUpdateObject (the latter after decompression).
		PacketParseResult HandleObjectUpdate(io::Reader &reader);

		PacketParseResult OnDestroyObjects(game::IncomingPacket &packet);

		PacketParseResult OnMovement(game::IncomingPacket &packet);

		PacketParseResult OnChatMessage(game::IncomingPacket &packet);

		PacketParseResult OnNameQueryResult(game::IncomingPacket &packet);

		PacketParseResult OnCreatureQueryResult(game::IncomingPacket &packet);

		PacketParseResult OnItemQueryResult(game::IncomingPacket &packet);

		PacketParseResult OnObjectQueryResult(game::IncomingPacket &packet);

		PacketParseResult OnInitialSpells(game::IncomingPacket &packet);

		PacketParseResult OnCreatureMove(game::IncomingPacket &packet);

		PacketParseResult OnSpellLearnedOrUnlearned(game::IncomingPacket &packet);

		PacketParseResult OnSpellStart(game::IncomingPacket &packet);

		PacketParseResult OnSpellGo(game::IncomingPacket &packet);

		PacketParseResult OnSpellCooldown(game::IncomingPacket &packet);

		PacketParseResult OnSpellFailure(game::IncomingPacket &packet);

		/// @brief Handles the ChannelStart packet from the server.
		PacketParseResult OnChannelStart(game::IncomingPacket &packet);

		/// @brief Handles the ChannelUpdate packet from the server.
		PacketParseResult OnChannelUpdate(game::IncomingPacket &packet);

		PacketParseResult OnAttackStart(game::IncomingPacket &packet);

		PacketParseResult OnAttackStop(game::IncomingPacket &packet);

		PacketParseResult OnAttackSwingError(game::IncomingPacket &packet);

		PacketParseResult OnXpLog(game::IncomingPacket &packet);

		PacketParseResult OnSpellDamageLog(game::IncomingPacket &packet);

		PacketParseResult OnNonSpellDamageLog(game::IncomingPacket &packet);

		PacketParseResult OnAttackerStateUpdate(game::IncomingPacket &packet);

		PacketParseResult OnLogEnvironmentalDamage(game::IncomingPacket &packet);

		PacketParseResult OnMovementSpeedChanged(game::IncomingPacket &packet);

		PacketParseResult OnForceMovementSpeedChange(game::IncomingPacket &packet);

		PacketParseResult OnMoveTeleport(game::IncomingPacket &packet);

		PacketParseResult OnLevelUp(game::IncomingPacket &packet);

		PacketParseResult OnAuraUpdate(game::IncomingPacket &packet);

		PacketParseResult OnPeriodicAuraLog(game::IncomingPacket &packet);

		PacketParseResult OnActionButtons(game::IncomingPacket &packet);

		PacketParseResult OnQuestGiverStatus(game::IncomingPacket &packet);

		PacketParseResult OnSpellEnergizeLog(game::IncomingPacket &packet);

		PacketParseResult OnTransferPending(game::IncomingPacket &packet);

		PacketParseResult OnNewWorld(game::IncomingPacket &packet);

		PacketParseResult OnGroupInvite(game::IncomingPacket &packet);

		PacketParseResult OnGroupDecline(game::IncomingPacket &packet);

		PacketParseResult OnPartyCommandResult(game::IncomingPacket &packet);

		PacketParseResult OnPartyPing(game::IncomingPacket &packet);

		PacketParseResult OnRandomRollResult(game::IncomingPacket &packet);

		PacketParseResult OnSpellHealLog(game::IncomingPacket &packet);

		PacketParseResult OnItemPushResult(game::IncomingPacket &packet);

		PacketParseResult OnLogoutResponse(game::IncomingPacket &packet);

		PacketParseResult OnMessageOfTheDay(game::IncomingPacket &packet);

		PacketParseResult OnMoveRoot(game::IncomingPacket &packet);
		PacketParseResult OnMoveStun(game::IncomingPacket &packet);
		PacketParseResult OnMoveSleep(game::IncomingPacket &packet);
		PacketParseResult OnMoveFear(game::IncomingPacket &packet);
		PacketParseResult OnMoveDisorient(game::IncomingPacket &packet);

		/// @brief Handles the GameTimeInfo packet.
		/// @param packet The incoming packet.
		/// @return The packet parse result.
		PacketParseResult OnGameTimeInfo(game::IncomingPacket &packet);

		PacketParseResult OnSetProficiency(game::IncomingPacket &packet);

		PacketParseResult OnSpellModChanged(game::IncomingPacket &packet);

		PacketParseResult OnTimePlayedResponse(game::IncomingPacket &packet);

		PacketParseResult OnTimeSyncRequest(game::IncomingPacket &packet);

		PacketParseResult OnDebugLineOfSightResult(game::IncomingPacket &packet);

	private:
#ifdef MMO_WITH_DEV_COMMANDS
		void Command_CheckLineOfSight(const std::string &cmd, const std::string &args) const;

		void Command_LearnSpell(const std::string &cmd, const std::string &args) const;

		void Command_CreateMonster(const std::string &cmd, const std::string &args) const;

		void Command_DestroyMonster(const std::string &cmd, const std::string &args) const;

		void Command_FaceMe(const std::string &cmd, const std::string &args) const;

		void Command_FollowMe(const std::string &cmd, const std::string &args) const;

		void Command_LevelUp(const std::string &cmd, const std::string &args) const;

		void Command_GiveMoney(const std::string &cmd, const std::string &args) const;

		void Command_AddItem(const std::string &cmd, const std::string &args) const;

		void Command_WorldPort(const std::string &cmd, const std::string &args) const;

		void Command_Speed(const std::string &cmd, const std::string &args) const;

		void Command_Summon(const std::string &cmd, const std::string &args) const;

		void Command_Port(const std::string &cmd, const std::string &args) const;

		void Command_Pos(const std::string &cmd, const std::string &args) const;

		void Command_TargetInfo(const std::string &cmd, const std::string &args) const;

		void Command_Morph(const std::string &cmd, const std::string &args) const;

		void Command_Kill(const std::string &cmd, const std::string &args) const;

		void Command_Revive(const std::string &cmd, const std::string &args) const;
#endif

	private:
		bool LoadMap();

		void OnChatNameQueryCallback(uint64 guid, const String &name);

		void OnAttackSwingErrorTimer();

		void EnqueueNextAttackSwingTimer();

		void OnItemPushCallback(const ItemInfo &itemInfo, uint64 characterGuid, bool wasLooted, bool wasCreated, uint8 bag, uint8 subslot, uint16 amount, uint16 totalCount);

	public:
		// Begin NetClient interface
		void SendAttackStart(const uint64 victim, const GameTime timestamp) override;

		void SendAttackStop(const GameTime timestamp) override;
		// End NetClient interface

	private:
		/// Adds a floating world text UI element with a duration to the world.
		void AddWorldTextFrame(const Vector3 &position, const String &text, const Color &color, float duration);

		void OnPageAvailabilityChanged(const PageNeighborhood &page, bool isAvailable) override;

		PagePosition GetPagePositionFromCamera() const;

		void EnsurePageIsLoaded(PagePosition position);

	private:
		RealmConnector &m_realmConnector;
		ScreenLayerIt m_paintLayer;
		scoped_connection_container m_realmConnections;
		scoped_connection_container m_inputConnections;
		std::unique_ptr<Scene> m_scene;
		std::unique_ptr<PlayerController> m_playerController;
		std::unique_ptr<AxisDisplay> m_debugAxis;
		std::unique_ptr<WorldGrid> m_worldGrid;
		uint32 m_lastZoneId = UINT32_MAX;
		IdGenerator<uint64> m_objectIdGenerator{1};
		IAudio &m_audio;

		bool m_combatMode = false;
		SceneNode *m_worldRootNode;
		std::shared_ptr<ClientWorldInstance> m_worldInstance;

		// Game time component for day/night cycle
		GameTimeComponent &m_gameTime;

		// Sky component for day/night cycle
		std::unique_ptr<SkyComponent> m_skyComponent;

		ICacheProvider &m_cache;

		scoped_connection_container m_playerObservers;
		scoped_connection_container m_targetObservers;

		const proto_client::Project &m_project;

		std::unique_ptr<ProjectileManager> m_projectileManager;

		/// @brief Structure to track pending projectile spawns waiting for SpellGo animation notify.
		struct PendingProjectile
		{
			uint32 spellId = 0;
			uint64 casterGuid = 0;
			uint64 targetGuid = 0;
			const proto_client::SpellVisualization* visualization = nullptr;
			bool hasCastSucceededAnimation = false;
			GameTime creationTime = 0;
			scoped_connection_container connections;
		};

		/// @brief List of pending projectiles waiting for SpellGo animation notify.
		std::vector<std::unique_ptr<PendingProjectile>> m_pendingProjectiles;

		TimerQueue &m_timers;
		AttackSwingEvent m_lastAttackSwingEvent{AttackSwingEvent::Unknown};

		Bindings m_bindings;

		std::vector<std::unique_ptr<WorldTextFrame>> m_worldTextFrames;

		std::unique_ptr<asio::io_service::work> m_work;
		asio::io_service m_workQueue;
		asio::io_service m_dispatcher;
		std::thread m_backgroundLoader;
		std::unique_ptr<LoadedPageSection> m_visibleSection;
		std::unique_ptr<WorldPageLoader> m_pageLoader;
		std::unique_ptr<PagePOVPartitioner> m_memoryPointOfView;
		LootClient &m_lootClient;
		VendorClient &m_vendorClient;

		std::unique_ptr<RaySceneQuery> m_rayQuery;

		RealmConnector::PacketHandlerHandleContainer m_worldPacketHandlers;
		RealmConnector::PacketHandlerHandleContainer m_worldChangeHandlers;

		ActionBar &m_actionBar;
		SpellCast &m_spellCast;
		CooldownManager &m_cooldownManager;
		TrainerClient &m_trainerClient;
		QuestClient &m_questClient;
		PartyInfo &m_partyInfo;

		SoundIndex m_backgroundMusicSound{InvalidSound};
		ChannelIndex m_backgroundMusicChannel{InvalidChannel};

		SoundIndex m_ambienceSound{InvalidSound};
		ChannelIndex m_ambienceChannel{InvalidChannel};

		CharSelect &m_charSelect;
		GuildClient &m_guildClient;
		FriendClient &m_friendClient;
		ChannelClient &m_channelClient;

		scoped_connection_container m_cvarChangedSignals;
		Discord &m_discord;

		std::unique_ptr<DebugPathVisualizer> m_debugPathVisualizer;

		/// Foliage system for rendering grass and other vegetation
		std::unique_ptr<Foliage> m_foliage;

		/// Key identifying a distinct procedural foliage layer derived from terrain material data:
		/// the painted terrain material (base or instance), the bound layer index and the mesh asset.
		using FoliageLayerKey = std::tuple<const MaterialInterface*, uint8, String>;

		/// A runtime foliage layer registered from terrain material data, ref-counted by the number
		/// of loaded terrain pages whose tiles reference it.
		struct RegisteredFoliageLayer
		{
			FoliageLayerPtr layer;
			uint32 refCount = 0;
		};

		/// All currently registered material-driven foliage layers, keyed by FoliageLayerKey.
		std::map<FoliageLayerKey, RegisteredFoliageLayer> m_foliageRegistry;

		/// Per terrain page (index = x + y * 64), the set of foliage layer keys it contributed,
		/// so they can be ref-decremented when the page unloads.
		std::map<uint16, std::vector<FoliageLayerKey>> m_pageFoliageKeys;

		TalentClient &m_talentClient;

		Minimap &m_minimap;

		InventoryClient &m_inventoryClient;

		TradeClient &m_tradeClient;

		bool m_worldLoaded = false;

		/// Active pings received from the server, ticked down each frame.
		std::vector<Minimap::PingDot> m_activePings;

		/// 3D world ping visualizer.
		std::unique_ptr<WorldPingVisualizer> m_worldPingVisualizer;

		bool m_timeSyncResponseSent = false;

		AreaTriggerManager m_areaTriggerManager;

	private:
		static IInputControl *s_inputControl;

	public:
		static IInputControl *GetInputControl()
		{
			return s_inputControl;
		}

		void GetPlayerName(uint64 guid, std::weak_ptr<GamePlayerC> player) override;

		void GetCreatureData(uint64 guid, std::weak_ptr<GameUnitC> creature) override;

		void GetItemData(uint64 guid, std::weak_ptr<GameItemC> item) override;

		void GetItemData(uint64 guid, std::weak_ptr<GamePlayerC> player) override;

		void OnMoveEvent(GameUnitC &unit, const MovementEvent &moveEvent) override;

		bool QueryWaterAt(float x, float z, float &outSurfaceY) const override;

		void SetSelectedTarget(uint64 guid) override;

		void OnGuildChanged(std::weak_ptr<GamePlayerC> player, uint64 guildGuid) override;

		void GetObjectData(uint64 guid, std::weak_ptr<GameWorldObjectC> object) override;
	};

}
