// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "game_states/game_state.h"
#include "scene_graph/mesh.h"
#include "screen.h"
#include "world_grid.h"

#include "frame_ui/frame_mgr.h"
#include "scene_graph/scene.h"
#include "scene_graph/camera.h"
#include "scene_graph/axis_display.h"
#include "base/signal.h"
#include "game_protocol/game_protocol.h"
#include "math/radian.h"


namespace mmo
{
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
		void SetupWorldScene();

	private:
		// 
		void OnRealmDisconnected();

		void OnEnterWorldFailed(game::player_login_response::Type error);

		void RegisterGameplayCommands();

		void RemoveGameplayCommands();

		void ToggleAxisVisibility();

	private:
		
		RealmConnector& m_realmConnector;
		ScreenLayerIt m_paintLayer;
		scoped_connection_container m_realmConnections;
		scoped_connection_container m_inputConnections;
		Scene m_scene;
		Camera* m_defaultCamera { nullptr };
		
		SceneNode* m_cameraAnchorNode { nullptr };
		SceneNode* m_cameraNode { nullptr };
		std::unique_ptr<AxisDisplay> m_debugAxis;

		Vector3 m_movementVelocity;
		SceneNode* m_playerNode { nullptr };
		Entity* m_playerEntity { nullptr };
		Radian m_rotation;

		std::unique_ptr<WorldGrid> m_worldGrid;
		
		bool m_leftButtonDown { false };
		bool m_rightButtonDown { false };
		Point m_lastMousePosition {};
	};
}
