// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "connection.h"
#include "player_controller.h"
#include "screen.h"
#include "game_states/game_state.h"

#include "base/signal.h"
#include "frame_ui/frame_mgr.h"
#include "game/game_object_c.h"
#include "game_protocol/game_protocol.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/scene.h"


namespace mmo
{
	namespace game
	{
		class IncomingPacket;
	}

	class LoginConnector;
	class RealmConnector;

	/// This class represents the initial game state where the player is asked to enter
	/// his credentials in order to authenticate.
	class WorldState final
		: public IGameState
	{
	public:
		explicit WorldState(RealmConnector& realmConnector);

	public:
		/// The default name of the world state
		static const std::string Name;

	public:
		// Inherited via IGameState
		void OnEnter() override;

		void OnLeave() override;

		const std::string& GetName() const override;

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

		void ToggleAxisVisibility();

		void ToggleGridVisibility();

	private:
				
		PacketParseResult OnUpdateObject(game::IncomingPacket& packet);
		
		PacketParseResult OnCompressedUpdateObject(game::IncomingPacket& packet);

		PacketParseResult OnDestroyObjects(game::IncomingPacket& packet);

	private:
		
		RealmConnector& m_realmConnector;
		ScreenLayerIt m_paintLayer;
		scoped_connection_container m_realmConnections;
		scoped_connection_container m_inputConnections;
		Scene m_scene;
		std::unique_ptr<PlayerController> m_playerController;
		std::unique_ptr<AxisDisplay> m_debugAxis;
		std::unique_ptr<WorldGrid> m_worldGrid;
		std::map<uint64, std::shared_ptr<GameObjectC>> m_gameObjectsById;
	};
}
