// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_ui/mouse_event_args.h"
#include "game/game_unit_c.h"
#include "game/movement.h"
#include "game_protocol/game_protocol.h"

namespace mmo
{
	class Scene;
	class SceneNode;
	class Camera;
	class RealmConnector;

	namespace ControlFlags
	{
		enum Type 
		{
			None,

			TurnPlayer = 1 << 0,
			TurnCamera = 1 << 1,
			MovePlayerOrTurnCamera = 1 << 2,
			MoveForwardKey = 1 << 3,
			MoveBackwardKey = 1 << 4,
			StrafeLeftKey = 1 << 5,
			StrafeRightKey = 1 << 6,
			TurnLeftKey = 1 << 7,
			TurnRightKey = 1 << 8,
			PitchUpKey = 1 << 9,
			PitchDownKey = 1 << 10,
			Autorun = 1 << 11,

			MoveSent = 1 << 12,
			StrafeSent = 1 << 13,
			TurnSent = 1 << 14,
			PitchSent = 1 << 15,
			
			MoveAndTurnPlayer = TurnPlayer | MovePlayerOrTurnCamera
		};
	}

	/// @brief This class controls a player entity.
	class PlayerController final
	{
	public:
		PlayerController(Scene& scene, RealmConnector& connector);

		~PlayerController();

	public:
		void Update(float deltaSeconds);
		
		void OnMouseDown(MouseButton button, int32 x, int32 y);

		void OnMouseUp(MouseButton button, int32 x, int32 y);

		void OnMouseMove(int32 x, int32 y);

		void OnMouseWheel(int32 delta);
		
		void OnKeyDown(int32 key);
		
		void OnKeyUp(int32 key);
		
		void SetControlledUnit(const std::shared_ptr<GameUnitC>& controlledUnit);

		[[nodiscard]] const std::shared_ptr<GameUnitC>& GetControlledUnit() const noexcept { return m_controlledUnit; }

		[[nodiscard]] Camera& GetCamera() const { ASSERT(m_defaultCamera); return *m_defaultCamera;}

		[[nodiscard]] SceneNode* GetRootNode() const { return m_controlledUnit ? m_controlledUnit->GetSceneNode() : nullptr; }

	private:
		void SetupCamera();

		void ResetControls();
		
		void MovePlayer();

		void StrafePlayer();

		void TurnPlayer();

		void ApplyLocalMovement(float deltaSeconds) const;
		
		void SendMovementUpdate(uint16 opCode) const;

		void StartHeartbeatTimer();

		void StopHeartbeatTimer();

		void UpdateHeartbeat();
		
		void NotifyCameraZoomChanged();

		void ClampCameraPitch();

	private:
		Scene& m_scene;
		RealmConnector& m_connector;
		Camera* m_defaultCamera { nullptr };
		SceneNode* m_cameraAnchorNode { nullptr };
		SceneNode* m_cameraNode { nullptr };
		std::shared_ptr<GameUnitC> m_controlledUnit;
		Point m_clickPosition{};
		bool m_leftButtonDown { false };
		bool m_rightButtonDown { false };
		Point m_lastMousePosition {};
		GameTime m_lastHeartbeat { 0 };
		uint32 m_controlFlags { ControlFlags::None };
		uint32 m_mouseDownTime = 0;

	};
}
