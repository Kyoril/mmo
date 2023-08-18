// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "player_controller.h"

#include "event_loop.h"
#include "vector_sink.h"
#include "net/realm_connector.h"
#include "console/console_var.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

#include "platform.h"

namespace mmo
{
	// Console variables for gameplay
	static ConsoleVar* s_mouseSensitivityCVar = nullptr;
	static ConsoleVar* s_invertVMouseCVar = nullptr;
	static ConsoleVar* s_maxCameraZoomCVar = nullptr;
	static ConsoleVar* s_cameraZoomCVar = nullptr;

	PlayerController::PlayerController(Scene& scene, RealmConnector& connector)
		: m_scene(scene)
		, m_connector(connector)
	{
		if (!s_mouseSensitivityCVar)
		{
			s_mouseSensitivityCVar = ConsoleVarMgr::RegisterConsoleVar("MouseSensitivity", "Gets or sets the mouse sensitivity value", "0.25");
			s_invertVMouseCVar = ConsoleVarMgr::RegisterConsoleVar("InvertVMouse", "Whether the vertical camera rotation is inverted.", "true");

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

		SetupCamera();
	}

	PlayerController::~PlayerController()
	{
		m_controlledUnit.reset();

		if (m_defaultCamera)
		{
			m_scene.DestroyCamera(*m_defaultCamera);
		}

		if (m_cameraNode)
		{
			m_scene.DestroySceneNode(*m_cameraNode);
		}

		if (m_cameraAnchorNode)
		{
			m_scene.DestroySceneNode(*m_cameraAnchorNode);
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
		auto* playerNode = m_controlledUnit->GetSceneNode();

		const auto& movementInfo = m_controlledUnit->GetMovementInfo();

		if (movementInfo.IsTurning())
		{
			if (movementInfo.movementFlags & MovementFlags::TurnLeft)
			{
				playerNode->Yaw(Degree(180) * deltaSeconds, TransformSpace::World);
			}
			else if (movementInfo.movementFlags & MovementFlags::TurnRight)
			{
				playerNode->Yaw(Degree(-180) * deltaSeconds, TransformSpace::World);
			}
		}

		if (movementInfo.IsMoving() || movementInfo.IsStrafing())
		{
			Vector3 movementVector;

			if (movementInfo.movementFlags & MovementFlags::Forward)
			{
				movementVector.z -= 1.0f;
			}
			if(movementInfo.movementFlags & MovementFlags::Backward)
			{
				movementVector.z += 1.0f;
			}
			if (movementInfo.movementFlags & MovementFlags::StrafeLeft)
			{
				movementVector.x -= 1.0f;
			}
			if(movementInfo.movementFlags & MovementFlags::StrafeRight)
			{
				movementVector.x += 1.0f;
			}

			playerNode->Translate(movementVector.NormalizedCopy() * 7.0f * deltaSeconds, TransformSpace::Local);
		}
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

	void PlayerController::Update(const float deltaSeconds)
	{
		if (!m_controlledUnit)
		{
			return;
		}
		
		int32 w, h;
		GraphicsDevice::Get().GetViewport(nullptr, nullptr, &w, &h);
		m_defaultCamera->SetAspectRatio(static_cast<float>(w) / static_cast<float>(h));

		MovePlayer();
		StrafePlayer();

		TurnPlayer();
		
		ApplyLocalMovement(deltaSeconds);
		UpdateHeartbeat();
	}

	void PlayerController::OnMouseDown(const MouseButton button, const int32 x, const int32 y)
	{
		m_lastMousePosition = Point(x, y);

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
		m_lastMousePosition = Point(x, y);
		
		if (button == MouseButton_Left)
		{
			m_controlFlags &= ~ControlFlags::TurnCamera;
		}
		else if (button == MouseButton_Right)
		{
			m_controlFlags &= ~ControlFlags::TurnPlayer;
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

		if ((m_controlFlags & (ControlFlags::TurnCamera | ControlFlags::TurnPlayer)) == 0)
		{
			return;
		}
		
		const Point position(x, y);
		const Point delta = position - m_lastMousePosition;
		m_lastMousePosition = position;

		SceneNode* yawNode = m_cameraAnchorNode;
		if (delta.x != 0.0f)
		{
			yawNode->Yaw(Degree(delta.x * s_mouseSensitivityCVar->GetFloatValue() * -1.0f), TransformSpace::Parent);
		}
		
		if (delta.y != 0.0f)
		{
			const float factor = s_invertVMouseCVar->GetBoolValue() ? -1.0f : 1.0f;

			const Radian deltaPitch = Degree(delta.y * factor * s_mouseSensitivityCVar->GetFloatValue());
			m_cameraAnchorNode->Pitch(deltaPitch, TransformSpace::Local);

			ClampCameraPitch();
		}

		if ((m_controlFlags & ControlFlags::TurnPlayer) != 0)
		{
			const Radian facing = m_cameraAnchorNode->GetDerivedOrientation().GetYaw();
			m_controlledUnit->GetSceneNode()->SetDerivedOrientation(Quaternion(facing, Vector3::UnitY));
			m_cameraAnchorNode->SetOrientation(
				Quaternion(m_cameraAnchorNode->GetOrientation().GetPitch(), Vector3::UnitX));

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

	void PlayerController::OnKeyDown(const int32 key)
	{
		switch(key)
		{
		case 0x57:
			m_controlFlags |= ControlFlags::MoveForwardKey;
			return;
		case 0x53:
			m_controlFlags |= ControlFlags::MoveBackwardKey;
			return;
		case 0x41:
			m_controlFlags |= ControlFlags::TurnLeftKey;
			break;
		case 0x44:
			m_controlFlags |= ControlFlags::TurnRightKey;
			break;
		case 81:
			m_controlFlags |= ControlFlags::StrafeLeftKey;
			break;
		case 69:
			m_controlFlags |= ControlFlags::StrafeRightKey;
			break;
		}
	}

	void PlayerController::OnKeyUp(const int32 key)
	{
		switch(key)
		{
		case 0x57:
			m_controlFlags &= ~ControlFlags::MoveForwardKey;
			break;
		case 0x53:
			m_controlFlags &= ~ControlFlags::MoveBackwardKey;
			break;
		case 0x41:
			m_controlFlags &= ~ControlFlags::TurnLeftKey;
			break;
		case 0x44:
			m_controlFlags &= ~ControlFlags::TurnRightKey;
			break;
		case 81:
			m_controlFlags &= ~ControlFlags::StrafeLeftKey;
			break;
		case 69:
			m_controlFlags &= ~ControlFlags::StrafeRightKey;
			break;
		}
	}
	
	void PlayerController::SetControlledUnit(const std::shared_ptr<GameUnitC>& controlledUnit)
	{
		m_cameraAnchorNode->RemoveFromParent();

		m_controlledUnit = controlledUnit;

		if (m_controlledUnit)
		{
			ASSERT(m_controlledUnit->GetSceneNode());
			m_controlledUnit->GetSceneNode()->AddChild(*m_cameraAnchorNode);
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
		//m_cameraAnchorNode->Yaw(Degree(180.0f), TransformSpace::Parent);

		NotifyCameraZoomChanged();
	}

	void PlayerController::ResetControls()
	{
		m_lastMousePosition = Point::Zero;
		m_leftButtonDown = false;
		m_rightButtonDown = false;
		m_controlFlags = ControlFlags::None;

		ASSERT(m_cameraNode);
		ASSERT(m_cameraAnchorNode);
		m_cameraNode->SetPosition(Vector3::UnitZ * 3.0f);
		m_cameraAnchorNode->SetOrientation(Quaternion::Identity);

		NotifyCameraZoomChanged();
	}
}
