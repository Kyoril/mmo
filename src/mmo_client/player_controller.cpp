// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "player_controller.h"

#include "event_loop.h"
#include "console/console_var.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	// Console variables for gameplay
	static ConsoleVar* s_mouseSensitivityCVar = nullptr;
	static ConsoleVar* s_invertVMouseCVar = nullptr;

	PlayerController::PlayerController(Scene& scene)
		: m_scene(scene)
	{
		if (!s_mouseSensitivityCVar)
		{
			s_mouseSensitivityCVar = ConsoleVarMgr::RegisterConsoleVar("MouseSensitivity", "Gets or sets the mouse sensitivity value", "0.25");
			s_invertVMouseCVar = ConsoleVarMgr::RegisterConsoleVar("InvertVMouse", "Whether the vertical camera rotation is inverted.", "true");
		}

		SetupCamera();
	}

	PlayerController::~PlayerController()
	{
		m_controlledObject.reset();

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

	void PlayerController::Update(float deltaSeconds)
	{
		if (!m_controlledObject)
		{
			return;
		}

		int32 w, h;
		GraphicsDevice::Get().GetViewport(nullptr, nullptr, &w, &h);
		m_defaultCamera->SetAspectRatio(static_cast<float>(w) / static_cast<float>(h));

		auto* playerNode = m_controlledObject->GetSceneNode();

		if (m_rightButtonDown)
		{
			playerNode->SetOrientation(Quaternion(m_cameraNode->GetDerivedOrientation().GetYaw(), Vector3::UnitY));
			m_cameraAnchorNode->SetOrientation(Quaternion(m_cameraAnchorNode->GetOrientation().GetPitch(), Vector3::UnitX));	
		}

		playerNode->Yaw(m_rotation * deltaSeconds, TransformSpace::World);

		// TODO: 7.0f = player movement speed
		if (m_movementVector.GetSquaredLength() != 0.0f)
		{
			playerNode->Translate(m_movementVector.NormalizedCopy() * 7.0f * deltaSeconds, TransformSpace::Local);
		}
	}

	void PlayerController::OnMouseDown(const MouseButton button, const int32 x, const int32 y)
	{
		m_lastMousePosition = Point(x, y);

		if (button == MouseButton_Left)
		{
			m_leftButtonDown = true;
		}
		else if (button == MouseButton_Right)
		{
			m_rightButtonDown = true;
		}
	}

	void PlayerController::OnMouseUp(const MouseButton button, const int32 x, const int32 y)
	{
		m_lastMousePosition = Point(x, y);

		if (button == MouseButton_Left)
		{
			m_leftButtonDown = false;
		}
		else if (button == MouseButton_Right)
		{
			m_rightButtonDown = false;
		}
	}

	void PlayerController::OnMouseMove(const int32 x, const int32 y)
	{
		if (!m_controlledObject)
		{
			return;
		}

		if (!m_leftButtonDown && !m_rightButtonDown)
		{
			return;
		}

		const Point position(x, y);
		const Point delta = position - m_lastMousePosition;
		m_lastMousePosition = position;

		SceneNode* yawNode = m_leftButtonDown ? m_cameraAnchorNode : m_controlledObject->GetSceneNode();
		if (delta.x != 0.0f)
		{
			yawNode->Yaw(Degree(delta.x * s_mouseSensitivityCVar->GetFloatValue() * -1.0f), TransformSpace::Parent);
		}
		
		if (delta.y != 0.0f)
		{
			const float factor = s_invertVMouseCVar->GetBoolValue() ? -1.0f : 1.0f;
			m_cameraAnchorNode->Pitch(Degree(delta.y * factor * s_mouseSensitivityCVar->GetFloatValue()), TransformSpace::Local);
		}
	}

	void PlayerController::OnMouseWheel(const int32 delta)
	{
		ASSERT(m_cameraNode);

		m_cameraNode->Translate(Vector3::UnitZ * static_cast<float>(delta), TransformSpace::Local);
	}

	void PlayerController::OnKeyDown(const int32 key)
	{
		switch(key)
		{
		case 0x57:
			m_movementVector.z = -1.0f;
			return;
		case 0x53:
			m_movementVector.z = 1.0f;
			return;
		case 0x41:
			if (m_rightButtonDown)
			{
				m_movementVector.x = -1.0f;
			}
			else
			{
				m_rotation = Degree(180.0f);	
			}
			return;
		case 0x44:
			if (m_rightButtonDown)
			{
				m_movementVector.x = 1.0f;
			}
			else
			{
				m_rotation = Degree(-180.0f);	
			}
		}
	}

	void PlayerController::OnKeyUp(const int32 key)
	{
		switch(key)
		{
		case 0x57:
		case 0x53:
			m_movementVector.z = 0.0f;
			return;
		case 0x41:
		case 0x44:
			if (m_rightButtonDown)
			{
				m_movementVector.x = 0.0f;
			}
			m_rotation = Degree(0.0f);
		}
	}
	
	void PlayerController::SetControlledObject(const std::shared_ptr<GameObjectC>& controlledObject)
	{
		m_cameraAnchorNode->RemoveFromParent();

		m_controlledObject = controlledObject;

		if (m_controlledObject)
		{
			ASSERT(m_controlledObject->GetSceneNode());
			m_controlledObject->GetSceneNode()->AddChild(*m_cameraAnchorNode);
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
		m_cameraAnchorNode->SetPosition(Vector3::UnitY * 0.5f);
	}

	void PlayerController::ResetControls()
	{
		m_lastMousePosition = Point::Zero;
		m_leftButtonDown = false;
		m_rightButtonDown = false;
		m_movementVector = Vector3::Zero;
		m_rotation = Radian(0.0f);

		ASSERT(m_cameraNode);
		ASSERT(m_cameraAnchorNode);
		m_cameraNode->SetPosition(Vector3::UnitZ * 3.0f);
		m_cameraAnchorNode->SetOrientation(Quaternion::Identity);
	}
}
