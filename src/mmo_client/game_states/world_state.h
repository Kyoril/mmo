// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "connection.h"
#include "db_cache.h"
#include "player_controller.h"
#include "screen.h"
#include "game_states/game_state.h"

#include "base/signal.h"
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

namespace mmo
{
	class TimerQueue;

	struct ItemInfo
	{
		uint64 id;
		String name;
		String description;
	};

	struct QuestInfo
	{
		uint64 id;
		String title;
		String summary;
		String description;
	};

	namespace game
	{
		class IncomingPacket;
	}

	class LoginConnector;
	class RealmConnector;

	class Entity;
	class SceneNode;

	/// This class represents the initial game state where the player is asked to enter
	/// his credentials in order to authenticate.
	class WorldState final
		: public GameState
		, public NetClient
		, public IPageLoaderListener
	{
	public:
		/// @brief Creates a new instance of the WorldState class and initializes it.
		/// @param gameStateManager The game state manager that this state belongs to.
		/// @param realmConnector The connector which manages the connection to the realm server.
		/// @param project 
		explicit WorldState(GameStateMgr& gameStateManager, RealmConnector& realmConnector, const proto_client::Project& project, TimerQueue& timers);

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
		// EventLoop connections
		
		bool OnMouseDown(MouseButton button, int32 x, int32 y);

		bool OnMouseUp(MouseButton button, int32 x, int32 y);

		bool OnMouseMove(int32 x, int32 y);

		bool OnMouseWheel(int32 delta);
		
		bool OnKeyDown(int32 key);	
		
		bool OnKeyUp(int32 key);

		void OnIdle(float deltaSeconds, GameTime timestamp);

		/// Called when the screen layer should be painted. Should paint the scene.
		void OnPaint();

	private:
		// Setup stuff

		void SetupWorldScene();

		void SetupPacketHandler();

		void RemovePacketHandler() const;
		
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

		PacketParseResult OnQuestQueryResult(game::IncomingPacket& packet);

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

		PacketParseResult OnLogEnvironmentalDamage(game::IncomingPacket& packet);

		PacketParseResult OnMovementSpeedChanged(game::IncomingPacket& packet);

		PacketParseResult OnForceMovementSpeedChange(game::IncomingPacket& packet);

	private:

#ifdef MMO_WITH_DEV_COMMANDS
		void Command_LearnSpell(const std::string& cmd, const std::string& args) const;

		void Command_CreateMonster(const std::string& cmd, const std::string& args) const;

		void Command_DestroyMonster(const std::string& cmd, const std::string& args) const;

		void Command_FaceMe(const std::string& cmd, const std::string& args) const;

		void Command_FollowMe(const std::string& cmd, const std::string& args) const;
#endif

		void Command_CastSpell(const std::string& cmd, const std::string& args);
		void Command_StartAttack(const std::string& cmd, const std::string& args);

	private:

		bool LoadMap(const String& assetPath);

		void CreateMapEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale);

		void OnChatNameQueryCallback(uint64 guid, const String& name);

		void OnAttackSwingErrorTimer();

		void EnqueueNextAttackSwingTimer();

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

	private:
		RealmConnector& m_realmConnector;
		ScreenLayerIt m_paintLayer;
		scoped_connection_container m_realmConnections;
		scoped_connection_container m_inputConnections;
		Scene m_scene;
		std::unique_ptr<PlayerController> m_playerController;
		std::unique_ptr<AxisDisplay> m_debugAxis;
		std::unique_ptr<WorldGrid> m_worldGrid;
		IdGenerator<uint64> m_objectIdGenerator{ 1 };

		SceneNode* m_cloudsNode { nullptr };
		Entity* m_cloudsEntity { nullptr };
		Light* m_sunLight { nullptr };

		SceneNode* m_worldRootNode;
		std::unique_ptr<ClientWorldInstance> m_worldInstance;

		DBCache<String, game::client_realm_packet::NameQuery> m_playerNameCache;

		DBCache<CreatureInfo, game::client_realm_packet::CreatureQuery> m_creatureCache;
		DBCache<ItemInfo, game::client_realm_packet::ItemQuery> m_itemCache;
		DBCache<QuestInfo, game::client_realm_packet::QuestQuery> m_questCache;

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
		std::unique_ptr<terrain::Terrain> m_terrain;

	private:
		static IInputControl* s_inputControl;

	public:
		static IInputControl* GetInputControl()
		{
			return s_inputControl;
		}

		void GetPlayerName(uint64 guid, std::weak_ptr<GamePlayerC> player) override;

		void GetCreatureData(uint64 guid, std::weak_ptr<GameUnitC> creature) override;
	};
}
