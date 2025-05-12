// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "audio.h"
#include "char_select.h"
#include "client_cache.h"
#include "connection.h"
#include "db_cache.h"
#include "player_controller.h"
#include "screen.h"
#include "game_states/game_state.h"


#include "base/signal.h"
#include "game/game_time_component.h"
#include "graphics/sky_component.h"
#include "game_client/game_object_c.h"
#include "game_protocol/game_protocol.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/light.h"
#include "scene_graph/scene.h"
#include "scene_graph/world_grid.h"
#include "spell_projectile.h"

#include "base/id_generator.h"
#include "world_deserializer.h"
#include "client_data/project.h"
#include "frame_ui/frame.h"
#include "game/action_button.h"
#include "game/auto_attack.h"
#include "paging/loaded_page_section.h"
#include "paging/page_loader_listener.h"
#include "paging/page_pov_partitioner.h"
#include "paging/world_page_loader.h"
#include "proto_data/proto_template.h"
#include "terrain/terrain.h"
#include "ui/binding.h"
#include "ui/world_text_frame.h"
#include "game/creature_data.h"
#include "game/quest_info.h"

#include "spell_cast.h"
#include "game_client/net_client.h"
#include "debug_path_visualizer.h"
#include "graphics/color_curve.h"
#include "graphics/material_instance.h"

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
	class Discord;

	/// This class represents the initial game state where the player is asked to enter
	/// his credentials in order to authenticate.
	class WorldState final
		: public GameState
		, public NetClient
		, public IPageLoaderListener
		, public ICollisionProvider
	{
	public:
		/// @brief Creates a new instance of the WorldState class and initializes it.
		/// @param gameStateManager The game state manager that this state belongs to.
		/// @param realmConnector The connector which manages the connection to the realm server.
		/// @param project 
		explicit WorldState(
			GameStateMgr& gameStateManager,
			RealmConnector& realmConnector,
			const proto_client::Project& project, 
			TimerQueue& timers,
			LootClient& lootClient,
			VendorClient& vendorClient,
			ActionBar& actionBar,
			SpellCast& spellCast,
			TrainerClient& trainerClient,
			QuestClient& questClient,
			IAudio& audio,
			PartyInfo& partyInfo,
			CharSelect& charSelect,
			GuildClient& guildClient, 
			ICacheProvider& cache,
			Discord& discord,
			GameTimeComponent& gameTime);

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

		void MovementIdleMoveUnits();

		void OnCombatModeChanged(uint64 monitoredGuid);

		void OnPlayerGuildChanged(uint64 monitoredGuid);

	private:
		// Selected Target Mirror Handlers (called when certain field map values of the selected target object were changed by the server)

		void OnTargetHealthChanged(uint64 monitoredGuid);

		void OnTargetPowerChanged(uint64 monitoredGuid);

		void OnTargetLevelChanged(uint64 monitoredGuid);

		void OnRenderShadowsChanged(ConsoleVar& var, const std::string& oldValue);

		void OnShadowBiasChanged(ConsoleVar& var, const std::string& oldValue);

		void OnShadowTextureSizeChanged(ConsoleVar& var, const std::string& oldValue);

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

	private:
		// Network packet handlers

		PacketParseResult OnUpdateObject(game::IncomingPacket& packet);
		
		PacketParseResult OnCompressedUpdateObject(game::IncomingPacket& packet);

		PacketParseResult OnDestroyObjects(game::IncomingPacket& packet);

		PacketParseResult OnMovement(game::IncomingPacket& packet);

		PacketParseResult OnChatMessage(game::IncomingPacket& packet);

		PacketParseResult OnNameQueryResult(game::IncomingPacket& packet);

		PacketParseResult OnCreatureQueryResult(game::IncomingPacket& packet);

		PacketParseResult OnItemQueryResult(game::IncomingPacket& packet);

		PacketParseResult OnObjectQueryResult(game::IncomingPacket& packet);

		PacketParseResult OnInitialSpells(game::IncomingPacket& packet);

		PacketParseResult OnCreatureMove(game::IncomingPacket& packet);

		PacketParseResult OnSpellLearnedOrUnlearned(game::IncomingPacket& packet);

		PacketParseResult OnSpellStart(game::IncomingPacket& packet);

		PacketParseResult OnSpellGo(game::IncomingPacket& packet);

		PacketParseResult OnSpellFailure(game::IncomingPacket& packet);

		PacketParseResult OnAttackStart(game::IncomingPacket& packet);

		PacketParseResult OnAttackStop(game::IncomingPacket& packet);

		PacketParseResult OnAttackSwingError(game::IncomingPacket& packet);

		PacketParseResult OnXpLog(game::IncomingPacket& packet);

		PacketParseResult OnSpellDamageLog(game::IncomingPacket& packet);

		PacketParseResult OnNonSpellDamageLog(game::IncomingPacket& packet);

		PacketParseResult OnAttackerStateUpdate(game::IncomingPacket& packet);

		PacketParseResult OnLogEnvironmentalDamage(game::IncomingPacket& packet);

		PacketParseResult OnMovementSpeedChanged(game::IncomingPacket& packet);

		PacketParseResult OnForceMovementSpeedChange(game::IncomingPacket& packet);

		PacketParseResult OnMoveTeleport(game::IncomingPacket& packet);

		PacketParseResult OnLevelUp(game::IncomingPacket& packet);

		PacketParseResult OnAuraUpdate(game::IncomingPacket& packet);

		PacketParseResult OnPeriodicAuraLog(game::IncomingPacket& packet);

		PacketParseResult OnActionButtons(game::IncomingPacket& packet);

		PacketParseResult OnQuestGiverStatus(game::IncomingPacket& packet);

		PacketParseResult OnSpellEnergizeLog(game::IncomingPacket& packet);

		PacketParseResult OnTransferPending(game::IncomingPacket& packet);

		PacketParseResult OnNewWorld(game::IncomingPacket& packet);

		PacketParseResult OnGroupInvite(game::IncomingPacket& packet);

		PacketParseResult OnGroupDecline(game::IncomingPacket& packet);

		PacketParseResult OnPartyCommandResult(game::IncomingPacket& packet);

		PacketParseResult OnRandomRollResult(game::IncomingPacket& packet);

		PacketParseResult OnSpellHealLog(game::IncomingPacket& packet);

		PacketParseResult OnItemPushResult(game::IncomingPacket& packet);

		PacketParseResult OnLogoutResponse(game::IncomingPacket& packet);

		PacketParseResult OnMessageOfTheDay(game::IncomingPacket& packet);
		PacketParseResult OnMoveRoot(game::IncomingPacket& packet);

		/// @brief Handles the GameTimeInfo packet.
		/// @param packet The incoming packet.
		/// @return The packet parse result.
		PacketParseResult OnGameTimeInfo(game::IncomingPacket& packet);

	private:

#ifdef MMO_WITH_DEV_COMMANDS
		void Command_LearnSpell(const std::string& cmd, const std::string& args) const;

		void Command_CreateMonster(const std::string& cmd, const std::string& args) const;

		void Command_DestroyMonster(const std::string& cmd, const std::string& args) const;

		void Command_FaceMe(const std::string& cmd, const std::string& args) const;

		void Command_FollowMe(const std::string& cmd, const std::string& args) const;

		void Command_LevelUp(const std::string& cmd, const std::string& args) const;

		void Command_GiveMoney(const std::string& cmd, const std::string& args) const;

		void Command_AddItem(const std::string& cmd, const std::string& args) const;

		void Command_WorldPort(const std::string& cmd, const std::string& args) const;

		void Command_Speed(const std::string& cmd, const std::string& args) const;

		void Command_Summon(const std::string& cmd, const std::string& args) const;

		void Command_Port(const std::string& cmd, const std::string& args) const;
#endif

	private:

		bool LoadMap();

		void OnChatNameQueryCallback(uint64 guid, const String& name);

		void OnAttackSwingErrorTimer();

		void EnqueueNextAttackSwingTimer();

		void OnItemPushCallback(const ItemInfo& itemInfo, uint64 characterGuid, bool wasLooted, bool wasCreated, uint8 bag, uint8 subslot, uint16 amount, uint16 totalCount);

	public:
		// Begin NetClient interface
		void SendAttackStart(const uint64 victim, const GameTime timestamp) override;

		void SendAttackStop(const GameTime timestamp) override;
		// End NetClient interface

	private:
		/// Adds a floating world text UI element with a duration to the world.
		void AddWorldTextFrame(const Vector3& position, const String& text, const Color& color, float duration);

		void OnPageAvailabilityChanged(const PageNeighborhood& page, bool isAvailable) override;

		PagePosition GetPagePositionFromCamera() const;

		void EnsurePageIsLoaded(PagePosition position);

	private:
		RealmConnector& m_realmConnector;
		ScreenLayerIt m_paintLayer;
		scoped_connection_container m_realmConnections;
		scoped_connection_container m_inputConnections;
		std::unique_ptr<Scene> m_scene;
		std::unique_ptr<PlayerController> m_playerController;
		std::unique_ptr<AxisDisplay> m_debugAxis;		std::unique_ptr<WorldGrid> m_worldGrid;
		IdGenerator<uint64> m_objectIdGenerator{ 1 };
		IAudio& m_audio;

		bool m_combatMode = false;

		SceneNode* m_worldRootNode;
		std::shared_ptr<ClientWorldInstance> m_worldInstance;

		// Game time component for day/night cycle
		GameTimeComponent& m_gameTime;
		
		// Sky component for day/night cycle
		std::unique_ptr<SkyComponent> m_skyComponent;

		ICacheProvider& m_cache;

		scoped_connection_container m_playerObservers;
		scoped_connection_container m_targetObservers;

		const proto_client::Project& m_project;

		std::vector<std::unique_ptr<SpellProjectile>> m_spellProjectiles;

		TimerQueue& m_timers;

		AttackSwingEvent m_lastAttackSwingEvent{ AttackSwingEvent::Unknown };

		Bindings m_bindings;

		std::vector<std::unique_ptr<WorldTextFrame>> m_worldTextFrames;

		std::unique_ptr<asio::io_service::work> m_work;
		asio::io_service m_workQueue;
		asio::io_service m_dispatcher;
		std::thread m_backgroundLoader;
		std::unique_ptr<LoadedPageSection> m_visibleSection;
		std::unique_ptr<WorldPageLoader> m_pageLoader;
		std::unique_ptr<PagePOVPartitioner> m_memoryPointOfView;
		LootClient& m_lootClient;
		VendorClient& m_vendorClient;

		std::unique_ptr<RaySceneQuery> m_rayQuery;

		RealmConnector::PacketHandlerHandleContainer m_worldPacketHandlers;
		RealmConnector::PacketHandlerHandleContainer m_worldChangeHandlers;

		ActionBar& m_actionBar;
		SpellCast& m_spellCast;
		TrainerClient& m_trainerClient;
		QuestClient& m_questClient;
		PartyInfo& m_partyInfo;

		SoundIndex m_backgroundMusicSound{ InvalidSound };
		ChannelIndex m_backgroundMusicChannel{ InvalidChannel };

		SoundIndex m_ambienceSound{ InvalidSound };
		ChannelIndex m_ambienceChannel{ InvalidChannel };

		CharSelect& m_charSelect;
		GuildClient& m_guildClient;

		scoped_connection_container m_cvarChangedSignals;
		Discord& m_discord;

		std::unique_ptr<DebugPathVisualizer> m_debugPathVisualizer;

	private:
		static IInputControl* s_inputControl;

	public:
		static IInputControl* GetInputControl()
		{
			return s_inputControl;
		}

		void GetPlayerName(uint64 guid, std::weak_ptr<GamePlayerC> player) override;

		void GetCreatureData(uint64 guid, std::weak_ptr<GameUnitC> creature) override;

		void GetItemData(uint64 guid, std::weak_ptr<GameItemC> item) override;

		void GetItemData(uint64 guid, std::weak_ptr<GamePlayerC> player) override;

		bool GetHeightAt(const Vector3& position, float range, float& out_height) override;

		void GetCollisionTrees(const AABB& aabb, std::vector<const Entity*>& out_potentialEntities) override;

		void OnMoveFallLand(GameUnitC& unit) override;

		void OnMoveFall(GameUnitC& unit) override;

		void SetSelectedTarget(uint64 guid) override;

		void OnGuildChanged(std::weak_ptr<GamePlayerC> player, uint64 guildGuid) override;

		bool GetGroundNormalAt(const Vector3& position, float range, Vector3& out_normal) override;

		void GetObjectData(uint64 guid, std::weak_ptr<GameWorldObjectC> object) override;
	};

}
