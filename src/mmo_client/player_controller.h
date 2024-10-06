// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "frame_ui/mouse_event_args.h"
#include "game_client/game_unit_c.h"
#include "game/movement.h"
#include "game_protocol/game_protocol.h"
#include "scene_graph/scene.h"
#include "input_control.h"
#include "base/vector.h"

namespace mmo
{
	class Scene;
	class SceneNode;
	class Camera;
	class RealmConnector;

	/// @brief This class controls a player entity.
	class PlayerController final : public IInputControl
	{
	public:
		PlayerController(Scene& scene, RealmConnector& connector);

		~PlayerController() override;

	public:
		void Update(float deltaSeconds);
		
		void OnMouseDown(MouseButton button, int32 x, int32 y);

		void OnMouseUp(MouseButton button, int32 x, int32 y);

		void OnMouseMove(int32 x, int32 y);

		void OnMouseWheel(int32 delta);
		
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

	public:
		void SetControlBit(const ControlFlags::Type flag, bool set) override { if (set) { m_controlFlags |= flag; } else { m_controlFlags &= ~flag; } }

	private:
		Scene& m_scene;
		std::unique_ptr<RaySceneQuery> m_selectionSceneQuery;
		RealmConnector& m_connector;
		Camera* m_defaultCamera { nullptr };
		SceneNode* m_cameraOffsetNode{ nullptr };
		SceneNode* m_cameraAnchorNode { nullptr };
		SceneNode* m_cameraNode { nullptr };
		std::shared_ptr<GameUnitC> m_controlledUnit;
		Vector<int32, 2> m_clickPosition{};
		bool m_leftButtonDown { false };
		bool m_rightButtonDown { false };
		Vector<int32, 2> m_lastMousePosition {};
		GameTime m_lastHeartbeat { 0 };
		uint32 m_controlFlags { ControlFlags::None };
		uint32 m_mouseDownTime = 0;

	};
}
