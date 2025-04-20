// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_ui/mouse_event_args.h"
#include "game_client/game_unit_c.h"
#include "game_protocol/game_protocol.h"
#include "scene_graph/scene.h"
#include "input_control.h"
#include "base/vector.h"

namespace mmo
{
	class VendorClient;
	class LootClient;
	class TrainerClient;
	class Scene;
	class SceneNode;
	class Camera;
	class RealmConnector;
	class SpellCast;

	/// @brief This class controls a player entity.
	class PlayerController final : public IInputControl
	{
	public:
		PlayerController(Scene& scene, RealmConnector& connector, LootClient& lootClient, VendorClient& vendorClient, TrainerClient& trainerClient, SpellCast& spellCast);

		~PlayerController() override;

	public:
		void StopAllMovement();

		void Update(float deltaSeconds);
		
		void OnMouseDown(MouseButton button, int32 x, int32 y);

		void OnMouseUp(MouseButton button, int32 x, int32 y);

		void OnMouseMove(int32 x, int32 y);

		void OnMouseWheel(int32 delta);
		
		void SetControlledUnit(const std::shared_ptr<GameUnitC>& controlledUnit);

		[[nodiscard]] const std::shared_ptr<GameUnitC>& GetControlledUnit() const noexcept { return m_controlledUnit; }

		[[nodiscard]] Camera& GetCamera() const { ASSERT(m_defaultCamera); return *m_defaultCamera;}

		[[nodiscard]] SceneNode* GetRootNode() const { return m_controlledUnit ? m_controlledUnit->GetSceneNode() : nullptr; }

		void OnMoveFallLand();

		void OnMoveFall();

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

		void OnMovementCompleted(GameUnitC& unit, const MovementInfo& movementInfo);

		void HandleCameraCollision();

	public:
		void SetControlBit(const ControlFlags::Type flag, bool set) override;

		void ToggleControlBit(const ControlFlags::Type flag) override { if (m_controlFlags & flag) { m_controlFlags &= ~flag; } else { m_controlFlags |= flag; } }

		void Jump() override;

		void OnHoveredObjectChanged(GameObjectC* previousHoveredUnit);

	private:
		Scene& m_scene;
		LootClient& m_lootClient;
		VendorClient& m_vendorClient;
		TrainerClient& m_trainerClient;
		std::unique_ptr<RaySceneQuery> m_selectionSceneQuery;
		RealmConnector& m_connector;
		SpellCast& m_spellCast;
		Camera* m_defaultCamera { nullptr };
		SceneNode* m_cameraOffsetNode{ nullptr };
		SceneNode* m_cameraAnchorNode { nullptr };
		SceneNode* m_cameraPitchNode{ nullptr };
		SceneNode* m_cameraNode { nullptr };
		std::shared_ptr<GameUnitC> m_controlledUnit;
		uint32 m_mouseMoved = 0;
		bool m_leftButtonDown { false };
		bool m_rightButtonDown { false };
		Vector<int32, 2> m_lastMousePosition {};
		GameTime m_lastHeartbeat { 0 };
		uint32 m_controlFlags { ControlFlags::None };
		uint32 m_mouseDownTime = 0;
		int32 m_x = 0, m_y = 0;
		GameObjectC* m_hoveredObject = nullptr;
		scoped_connection_container m_cvarConnections;
		scoped_connection m_moveCompleted;
		GameTime m_nextSetFacing = 0;

		Vector3 m_desiredCameraLocation;
	};
}
