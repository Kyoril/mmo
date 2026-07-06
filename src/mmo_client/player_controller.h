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
	class BankClient;
	class Scene;
	class SceneNode;
	class Camera;
	class RealmConnector;
	class SpellCast;
	class Movement;

	/// @brief This class controls a player entity.
	class PlayerController final : public IInputControl
	{
	public:
		PlayerController(Scene& scene, RealmConnector& connector, LootClient& lootClient, VendorClient& vendorClient, TrainerClient& trainerClient, SpellCast& spellCast, BankClient& bankClient);

		~PlayerController() override;

	public:
		void StopAllMovement();

		void Update(float deltaSeconds);
		
		void OnMouseDown(MouseButton button, int32 x, int32 y);

		void OnMouseUp(MouseButton button, int32 x, int32 y);

		void OnMouseMove(int32 x, int32 y);

		void OnMouseWheel(int32 delta);
		
		void SetControlledUnit(const std::shared_ptr<GameUnitC>& controlledUnit);

		[[nodiscard]] const std::shared_ptr<GameUnitC>& GetControlledUnit() const { return m_controlledUnit; }

		[[nodiscard]] Camera& GetCamera() const { ASSERT(m_defaultCamera); return *m_defaultCamera;}

		[[nodiscard]] SceneNode* GetRootNode() const { return m_controlledUnit ? m_controlledUnit->GetSceneNode() : nullptr; }

		[[nodiscard]] GameObjectC* GetHoveredObject() const { return m_hoveredObject; }

		/// Performs the default right-click interaction with a game object: attack living
		/// enemies, talk to / trade with friendly NPCs, loot corpses, use world objects.
		/// Shared by the world right-click and the nameplate right-click.
		void InteractWithObject(GameObjectC& object);

		[[nodiscard]] int32 GetMouseX() const { return m_x; }
		[[nodiscard]] int32 GetMouseY() const { return m_y; }

		/// @brief Adds camera shake "trauma". Trauma decays over time and drives a screen shake
		/// whose magnitude scales with trauma squared, so light hits stay subtle. Clamped to [0,1].
		/// @param amount Trauma to add (roughly 0.15 for a light hit, up to ~0.6 for a heavy/critical hit).
		void AddTrauma(float amount);

	private:
		void SetupCamera();

		void ResetControls();
		
		void MovePlayer();

		void StrafePlayer();

		void TurnPlayer();

		void SendMovementUpdate(uint16 opCode) const;

		void SendMovementUpdateWithInfo(uint16 opCode, const MovementInfo& movementInfo) const;

		void NotifyCameraZoomChanged();

		void ClampCameraPitch();

		void OnMovementCompleted(GameUnitC& unit, const MovementInfo& movementInfo);

		void HandleCameraCollision();

		/// @brief Decays camera shake trauma and applies the resulting screen-shake offset to the
		/// camera node on top of its collision-resolved position. Called once per frame.
		void UpdateCameraShake(float deltaSeconds);

		void SetOrbitModeEnabled(bool enable);

	public:
		void SetControlBit(const ControlFlags::Type flag, bool set) override;

		void ToggleControlBit(const ControlFlags::Type flag) override { if (m_controlFlags & flag) { m_controlFlags &= ~flag; } else { m_controlFlags |= flag; } }

		void Jump() override;

		void StopJump() override;

		void ToggleWalkMode() override;

		void OnHoveredObjectChanged(GameObjectC* previousHoveredUnit);

		void ProcessMovementEvent(const MovementEvent& movementEvent);

	private:
		Scene& m_scene;
		LootClient& m_lootClient;
		VendorClient& m_vendorClient;
		TrainerClient& m_trainerClient;
		BankClient& m_bankClient;
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
		/// GUID of the unit currently showing the hover ring, resolved through ObjectMgr so a
		/// despawned unit is handled safely (no dangling pointer). 0 when nothing is highlighted.
		ObjectGuid m_hoverRingGuid = 0;
		scoped_connection_container m_cvarConnections;
		scoped_connection m_moveCompleted;
		GameTime m_nextSetFacing = 0;

		/// Character dive pitch while swimming. Controlled only by right-mouse vertical drag,
		/// clamped, and reset to zero when not swimming. Independent of the visual camera pitch.
		Radian m_swimPitch { 0.0f };
		/// True while the jump key is held; under water this drives an upward swim.
		bool m_swimAscend { false };

		Vector3 m_desiredCameraLocation;
		Quaternion m_savedOrientation;

		/// @brief Current camera-shake trauma in [0,1]; decays to zero over ~0.4s.
		float m_cameraTrauma { 0.0f };
		/// @brief Time accumulator driving the shake noise while trauma is active.
		float m_cameraShakeTime { 0.0f };
	};
}
