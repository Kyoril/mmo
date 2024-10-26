// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "player_controller.h"

#include "cursor.h"
#include "event_loop.h"
#include "vector_sink.h"
#include "net/realm_connector.h"
#include "console/console_var.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

#include "platform.h"
#include "console/console.h"
#include "frame_ui/frame_mgr.h"

namespace mmo
{
	// Console variables for gameplay
	static ConsoleVar* s_mouseSensitivityCVar = nullptr;
	static ConsoleVar* s_invertVMouseCVar = nullptr;
	static ConsoleVar* s_maxCameraZoomCVar = nullptr;
	static ConsoleVar* s_cameraZoomCVar = nullptr;

	extern Cursor g_cursor;

	PlayerController::PlayerController(Scene& scene, RealmConnector& connector)
		: m_scene(scene)
		, m_connector(connector)
	{
		if (!s_mouseSensitivityCVar)
		{
			s_mouseSensitivityCVar = ConsoleVarMgr::RegisterConsoleVar("MouseSensitivity", "Gets or sets the mouse sensitivity value", "0.25");
			s_invertVMouseCVar = ConsoleVarMgr::RegisterConsoleVar("InvertVMouse", "Whether the vertical camera rotation is inverted.", "1");

			s_maxCameraZoomCVar = ConsoleVarMgr::RegisterConsoleVar("MaxCameraZoom", "Gets or sets the maximum camera zoom value.", "8");
			s_maxCameraZoomCVar->Changed += [this](ConsoleVar&, const std::string&) 
			{
				NotifyCameraZoomChanged();
			};

			s_cameraZoomCVar = ConsoleVarMgr::RegisterConsoleVar("CameraZoom", "Gets or sets the current camera zoom value.", "8");
			s_cameraZoomCVar->Changed += [this](ConsoleVar&, const std::string&) 
			{
				NotifyCameraZoomChanged();
			};
		}

		m_selectionSceneQuery = m_scene.CreateRayQuery(Ray());
		m_selectionSceneQuery->SetQueryMask(0xF0000000);

		SetupCamera();
	}

	PlayerController::~PlayerController()
	{
		m_controlledUnit.reset();

		if (m_defaultCamera)
		{
			m_scene.DestroyCamera(*m_defaultCamera);
			m_defaultCamera = nullptr;
		}

		if (m_cameraNode)
		{
			m_scene.DestroySceneNode(*m_cameraNode);
			m_cameraNode = nullptr;
		}

		if (m_cameraAnchorNode)
		{
			m_scene.DestroySceneNode(*m_cameraAnchorNode);
			m_cameraAnchorNode = nullptr;
		}

		if (m_cameraOffsetNode)
		{
			m_scene.DestroySceneNode(*m_cameraOffsetNode);
			m_cameraOffsetNode = nullptr;
		}
	}

	void PlayerController::MovePlayer()
	{
		int movementDirection = (m_controlFlags & ControlFlags::Autorun) != 0;

		if (m_controlFlags & ControlFlags::MoveForwardKey)
		{
			++movementDirection;
		}
		if (m_controlFlags & ControlFlags::MoveBackwardKey)
		{
			--movementDirection;
		}

		if (!m_controlledUnit->IsAlive())
		{
			movementDirection = 0;
		}

		if (movementDirection != 0)
		{
			if (m_controlFlags & ControlFlags::MoveSent)
			{
				return;
			}

			const bool forward = movementDirection > 0;
			m_controlledUnit->StartMove(forward);
			SendMovementUpdate(forward ? game::client_realm_packet::MoveStartForward : game::client_realm_packet::MoveStartBackward);
			StartHeartbeatTimer();
			
			m_controlFlags |= ControlFlags::MoveSent;
			return;
		}

		if((m_controlFlags & ControlFlags::MoveSent) != 0)
		{
			m_controlledUnit->StopMove();
			SendMovementUpdate(game::client_realm_packet::MoveStop);
			StopHeartbeatTimer();
			m_controlFlags &= ~ControlFlags::MoveSent;
		}
	}

	void PlayerController::StrafePlayer()
	{
		int direction = (m_controlFlags & ControlFlags::StrafeLeftKey) != 0;
		if ((m_controlFlags & ControlFlags::TurnPlayer) != 0 && (m_controlFlags & ControlFlags::TurnLeftKey) != 0)
		{
			++direction;
		}

		if ((m_controlFlags & ControlFlags::StrafeRightKey) != 0)
		{
			--direction;
		}

		if ((m_controlFlags & ControlFlags::TurnPlayer) != 0 && (m_controlFlags & ControlFlags::TurnRightKey) != 0)
		{
			--direction;
		}


		if (!m_controlledUnit->IsAlive())
		{
			direction = 0;
		}

		if (direction != 0)
		{
			if ((m_controlFlags & ControlFlags::StrafeSent) != 0)
			{
				return;
			}

			const bool left = direction > 0;
			m_controlledUnit->StartStrafe(left);
			SendMovementUpdate(left ? game::client_realm_packet::MoveStartStrafeLeft : game::client_realm_packet::MoveStartStrafeRight);
			StartHeartbeatTimer();

			m_controlFlags |= ControlFlags::StrafeSent;
			return;
		}

		if (m_controlFlags & ControlFlags::StrafeSent)
		{
			m_controlledUnit->StopStrafe();
			m_controlFlags &= ~ControlFlags::StrafeSent;
		}
	}

	void PlayerController::TurnPlayer()
	{
		if ((m_controlFlags & ControlFlags::TurnPlayer) == 0)
		{
			int direction = ((m_controlFlags & ControlFlags::TurnLeftKey) != 0);
			if ((m_controlFlags & ControlFlags::TurnRightKey))
			{
				--direction;
			}

			if (!m_controlledUnit->IsAlive())
			{
				direction = 0;
			}

			if (direction != 0)
			{
				if ((m_controlFlags & ControlFlags::TurnSent))
				{
					return;
				}
				
				const bool left = direction > 0;
				m_controlledUnit->StartTurn(left);
				SendMovementUpdate(left ? game::client_realm_packet::MoveStartTurnLeft : game::client_realm_packet::MoveStartTurnRight);
				m_controlFlags |= ControlFlags::TurnSent;
			}
			else if (m_controlFlags & ControlFlags::TurnSent)
			{
				m_controlledUnit->StopTurn();
				SendMovementUpdate(game::client_realm_packet::MoveStopTurn);
				m_controlFlags &= ~ControlFlags::TurnSent;
			}
		}
	}

	void PlayerController::ApplyLocalMovement(const float deltaSeconds) const
	{
	}

	void PlayerController::SendMovementUpdate(const uint16 opCode) const
	{
		// Build the movement data
		MovementInfo info;
		info.timestamp = GetAsyncTimeMs();
		info.position = m_controlledUnit->GetSceneNode()->GetDerivedPosition();
		info.facing = m_controlledUnit->GetSceneNode()->GetDerivedOrientation().GetYaw();
		info.pitch = Radian(0);
		info.fallTime = 0; // TODO
		info.jumpCosAngle = 0.0f; // TODO
		info.jumpSinAngle = 0.0f; // TODO
		info.jumpVelocity = 0.0f; // TODO
		info.jumpXZSpeed = 0.0f; // TODO
		info.movementFlags = m_controlledUnit->GetMovementInfo().movementFlags;
		m_connector.SendMovementUpdate(m_controlledUnit->GetGuid(), opCode, info);
	}

	void PlayerController::StartHeartbeatTimer()
	{
		if (m_lastHeartbeat != 0)
		{
			return;
		}

		m_lastHeartbeat = GetAsyncTimeMs();
	}

	void PlayerController::StopHeartbeatTimer()
	{
		m_lastHeartbeat = 0;
	}

	void PlayerController::UpdateHeartbeat()
	{
		if (m_lastHeartbeat == 0)
		{
			return;
		}

		const auto now = GetAsyncTimeMs();
		if (now - m_lastHeartbeat >= 500)
		{
			SendMovementUpdate(game::client_realm_packet::MoveHeartBeat);
			m_lastHeartbeat = now;
		}
	}

	void PlayerController::NotifyCameraZoomChanged()
	{
		ASSERT(s_maxCameraZoomCVar);
		ASSERT(s_cameraZoomCVar);

		const float maxZoom = Clamp<float>(s_maxCameraZoomCVar->GetFloatValue(), 2.0f, 15.0f);
		const float newZoom = Clamp<float>(s_cameraZoomCVar->GetFloatValue(), 0.0f, maxZoom);
		m_cameraNode->SetPosition(m_cameraNode->GetOrientation() * (Vector3::UnitZ * newZoom));
	}

	void PlayerController::ClampCameraPitch()
	{
		const float clampDegree = 60.0f;

		// Ensure the camera pitch is clamped
		const Radian pitch = m_cameraAnchorNode->GetOrientation().GetPitch();
		if (pitch < Degree(-clampDegree))
		{
			m_cameraAnchorNode->Pitch(Degree(-clampDegree) - pitch, TransformSpace::Local);
		}
		if (pitch > Degree(clampDegree))
		{
			m_cameraAnchorNode->Pitch(Degree(clampDegree) - pitch, TransformSpace::Local);
		}
	}

	void PlayerController::OnHoveredUnitChanged(GameUnitC* previousHoveredUnit)
	{
		if (m_hoveredUnit)
		{
			g_cursor.SetCursorType(CursorType::Attack);
		}
		else
		{
			g_cursor.SetCursorType(CursorType::Pointer);
		}

		// TODO: Raise UI event?

	}

	void PlayerController::Update(const float deltaSeconds)
	{
		if (!m_controlledUnit)
		{
			return;
		}

		int32 w, h;
		GraphicsDevice::Get().GetViewport(nullptr, nullptr, &w, &h);
		m_defaultCamera->SetAspectRatio(static_cast<float>(w) / static_cast<float>(h));
		m_defaultCamera->InvalidateView();

		MovePlayer();
		StrafePlayer();

		TurnPlayer();
		
		ApplyLocalMovement(deltaSeconds);
		UpdateHeartbeat();

		// Fire unit raycast
		m_selectionSceneQuery->ClearResult();
		m_selectionSceneQuery->SetSortByDistance(true);
		m_selectionSceneQuery->SetRay(m_defaultCamera->GetCameraToViewportRay(
			static_cast<float>(m_x) / static_cast<float>(w),
			static_cast<float>(m_y) / static_cast<float>(h), 1000.0f));
		m_selectionSceneQuery->Execute();

		const uint64 previousSelectedUnit = m_controlledUnit->Get<uint64>(object_fields::TargetUnit);

		GameUnitC* newHoveredUnit = nullptr;

		const auto& hitResult = m_selectionSceneQuery->GetLastResult();
		if (!hitResult.empty())
		{
			Entity* entity = static_cast<Entity*>(hitResult[0].movable);
			if (entity)
			{
				newHoveredUnit = entity->GetUserObject<GameUnitC>();
			}
		}

		if (newHoveredUnit != m_hoveredUnit)
		{
			GameUnitC* previousUnit = m_hoveredUnit;
			m_hoveredUnit = newHoveredUnit;

			OnHoveredUnitChanged(previousUnit);
		}
	}

	void PlayerController::OnMouseDown(const MouseButton button, const int32 x, const int32 y)
	{
		m_x = x;
		m_y = y;

		m_mouseDownTime = GetAsyncTimeMs();
		m_mouseMoved = 0;

		if ((m_controlFlags & (ControlFlags::TurnCamera | ControlFlags::TurnPlayer)) == 0)
		{
			POINT cursorPos;
			GetCursorPos(&cursorPos);
			m_lastMousePosition[0] = cursorPos.x;
			m_lastMousePosition[1] = cursorPos.y;
		}

		if (button == MouseButton_Left)
		{
			m_controlFlags |= ControlFlags::TurnCamera;
		}
		else if (button == MouseButton_Right)
		{
			m_controlFlags |= ControlFlags::TurnPlayer;
		}

		if (button == MouseButton_Left || button == MouseButton_Right)
		{
			Platform::CaptureMouse();
		}
	}

	void PlayerController::OnMouseUp(const MouseButton button, const int32 x, const int32 y)
	{
		m_x = x;
		m_y = y;

		if (button == MouseButton_Left)
		{
			m_controlFlags &= ~ControlFlags::TurnCamera;
		}
		else if (button == MouseButton_Right)
		{
			m_controlFlags &= ~ControlFlags::TurnPlayer;
		}

		if (m_mouseMoved <= 16)
		{
			const uint64 previousSelectedUnit = m_controlledUnit->Get<uint64>(object_fields::TargetUnit);

			if (m_hoveredUnit)
			{
				if (m_hoveredUnit->GetGuid() != previousSelectedUnit)
				{
					m_connector.SetSelection(m_hoveredUnit->GetGuid());
				}

				if (button == MouseButton_Right)
				{
					// TODO: Support more interactions than auto attack
					m_controlledUnit->Attack(*m_hoveredUnit);
				}
			}
			else
			{
				if (previousSelectedUnit != 0)
				{
					m_connector.SetSelection(0);
				}
			}
		}

		if (button == MouseButton_Left || button == MouseButton_Right && 
			(m_controlFlags & (ControlFlags::TurnCamera | ControlFlags::TurnPlayer)) == 0)
		{
			Platform::ReleaseMouseCapture();
		}
	}

	void PlayerController::OnMouseMove(const int32 x, const int32 y)
	{
		if (!m_controlledUnit)
		{
			return;
		}

		m_x = x;
		m_y = y;

		if ((m_controlFlags & (ControlFlags::TurnCamera | ControlFlags::TurnPlayer)) == 0)
		{
			return;
		}

		// Get the local mouse position and calculate delta to the last mouse position
		int32 cursorX, cursorY;
		Platform::GetCursorPos(cursorX, cursorY);
		const int32 deltaX = cursorX - m_lastMousePosition.x();
		const int32 deltaY = cursorY - m_lastMousePosition.y();

		m_mouseMoved += std::abs(deltaX) + std::abs(deltaY);

		// Reset platform mouse cursor to captured position to prevent it from reaching the edge of the screen
		Platform::ResetCursorPosition();

		SceneNode* yawNode = m_cameraAnchorNode;
		if (std::abs(deltaX) > 0)
		{
			yawNode->Yaw(Degree(static_cast<float>(deltaX) * s_mouseSensitivityCVar->GetFloatValue() * -1.0f), TransformSpace::Parent);
		}
		
		if (std::abs(deltaY) > 0)
		{
			const float factor = s_invertVMouseCVar->GetBoolValue() ? -1.0f : 1.0f;

			const Radian deltaPitch = Degree(static_cast<float>(deltaY) * factor * s_mouseSensitivityCVar->GetFloatValue());
			m_cameraAnchorNode->Pitch(deltaPitch, TransformSpace::Local);

			ClampCameraPitch();
		}

		if ((m_controlFlags & ControlFlags::TurnPlayer) != 0 && m_controlledUnit->IsAlive())
		{
			const Radian facing = (m_controlledUnit->GetSceneNode()->GetOrientation() * m_cameraAnchorNode->GetOrientation()).GetYaw();
			m_controlledUnit->GetSceneNode()->SetOrientation(Quaternion(facing, Vector3::UnitY));
			m_cameraAnchorNode->SetOrientation(
				Quaternion(m_cameraAnchorNode->GetOrientation().GetPitch(false), Vector3::UnitX));

			m_controlledUnit->SetFacing(facing);
			SendMovementUpdate(game::client_realm_packet::MoveSetFacing);
		}
	}

	void PlayerController::OnMouseWheel(const int32 delta)
	{
		ASSERT(m_cameraNode);

		const float currentZoom = m_cameraNode->GetPosition().z;
		s_cameraZoomCVar->Set(currentZoom - static_cast<float>(delta));
	}
		
	void PlayerController::SetControlledUnit(const std::shared_ptr<GameUnitC>& controlledUnit)
	{
		m_cameraOffsetNode->RemoveFromParent();

		m_controlledUnit = controlledUnit;

		if (m_controlledUnit)
		{
			ASSERT(m_controlledUnit->GetSceneNode());
			m_controlledUnit->GetSceneNode()->AddChild(*m_cameraOffsetNode);
		}
	}

	void PlayerController::SetupCamera()
	{
		// Default camera for the player
		m_defaultCamera = m_scene.CreateCamera("Default");
		
		// Camera node which will hold the camera but is a child of an anchor node
		m_cameraNode = &m_scene.CreateSceneNode("DefaultCamera");
		m_cameraNode->AttachObject(*m_defaultCamera);
		m_cameraNode->SetPosition(Vector3(0.0f, 0.0f, 3.0f));

		// Anchor node for the camera. This node is directly attached to the player node and marks
		// the target view point of the camera. By adding the camera node as a child, we can rotate
		// the anchor node which results in the camera orbiting around the player entity.
		m_cameraAnchorNode = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraAnchorNode->AddChild(*m_cameraNode);
		m_cameraAnchorNode->SetPosition(Vector3::UnitY);

		m_cameraOffsetNode = &m_scene.CreateSceneNode("CameraOffset");
		m_cameraOffsetNode->AddChild(*m_cameraAnchorNode);
		m_cameraOffsetNode->Yaw(Degree(-90.0f), TransformSpace::Parent);

		NotifyCameraZoomChanged();
	}

	void PlayerController::ResetControls()
	{
		m_lastMousePosition.clear();
		m_leftButtonDown = false;
		m_rightButtonDown = false;
		m_controlFlags = ControlFlags::None;

		ASSERT(m_cameraNode);
		ASSERT(m_cameraAnchorNode);
		m_cameraNode->SetPosition(Vector3::UnitZ * 3.0f);

		const Quaternion defaultRotation(Degree(0.0f), Vector3::UnitY);
		m_cameraAnchorNode->SetOrientation(defaultRotation);

		NotifyCameraZoomChanged();
	}
}
