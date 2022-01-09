// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "world_state.h"
#include "game_state_mgr.h"

#include "event_loop.h"
#include "login_state.h"
#include "net/realm_connector.h"
#include "ui/world_frame.h"
#include "ui/world_renderer.h"
#include "console/console.h"
#include "console/console_var.h"

#include "assets/asset_registry.h"
#include "frame_ui/frame_mgr.h"
#include "scene_graph/entity.h"

namespace mmo
{
	const std::string WorldState::Name = "world";

	extern CharacterView s_selectedCharacter;

	// Console variables for gameplay
	static ConsoleVar* s_mouseSensitivityCVar = nullptr;
	static ConsoleVar* s_invertVMouseCVar = nullptr;

	// Console command names
	namespace command_names
	{
		static const char* ToggleAxis = "ToggleAxis";	
	}
	
	WorldState::WorldState(RealmConnector& realmConnector)
		: m_realmConnector(realmConnector)
	{
	}

	void WorldState::OnEnter()
	{
		SetupWorldScene();

		if (!s_mouseSensitivityCVar)
		{
			s_mouseSensitivityCVar = ConsoleVarMgr::RegisterConsoleVar("MouseSensitivity", "Gets or sets the mouse sensitivity value", "0.25");
			s_invertVMouseCVar = ConsoleVarMgr::RegisterConsoleVar("InvertVMouse", "Whether the vertical camera rotation is inverted.", "true");
		}

		// Register world renderer
		FrameManager::Get().RegisterFrameRenderer("WorldRenderer", [this](const std::string& name)
			{
				return std::make_unique<WorldRenderer>(name, std::ref(m_scene));
			});

		// Register world frame type
		FrameManager::Get().RegisterFrameFactory("World", [](const std::string& name) {
			return std::make_shared<WorldFrame>(name);
			});
		
		// Make the top frame element
		const auto topFrame = FrameManager::Get().CreateOrRetrieve("Frame", "TopGameFrame");
		topFrame->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr);
		topFrame->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr);
		topFrame->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr);
		topFrame->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr);
		FrameManager::Get().SetTopFrame(topFrame);

		// Load ui file
		FrameManager::Get().LoadUIFile("Interface/GameUI/GameUI.toc");

		m_realmConnections += {
			m_realmConnector.EnterWorldFailed.connect(*this, &WorldState::OnEnterWorldFailed),
			m_realmConnector.Disconnected.connect(*this, &WorldState::OnRealmDisconnected)
		};

		// Send enter world request to server
		m_realmConnector.EnterWorld(s_selectedCharacter);

		// Register drawing of the game ui
		m_paintLayer = Screen::AddLayer(std::bind(&WorldState::OnPaint, this), 1.0f, ScreenLayerFlags::IdentityTransform);

		m_inputConnections += {
			EventLoop::MouseDown.connect(this, &WorldState::OnMouseDown),
			EventLoop::MouseUp.connect(this, &WorldState::OnMouseUp),
			EventLoop::MouseMove.connect(this, &WorldState::OnMouseMove),
			EventLoop::KeyDown.connect(this, &WorldState::OnKeyDown),
			EventLoop::KeyUp.connect(this, &WorldState::OnKeyUp)
		};
		
		RegisterGameplayCommands();
	}

	void WorldState::OnLeave()
	{
		RemoveGameplayCommands();

		m_worldGrid.reset();
		m_playerEntity = nullptr;
		m_playerNode = nullptr;
		m_defaultCamera = nullptr;
		m_scene.Clear();

		m_inputConnections.disconnect();
		m_realmConnections.disconnect();

		// Reset the logo frame ui
		FrameManager::Get().ResetTopFrame();
		
		// Remove world renderer
		FrameManager::Get().RemoveFrameRenderer("WorldRenderer");
		FrameManager::Get().UnregisterFrameFactory("World");
		
		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);
	}

	const std::string& WorldState::GetName() const
	{
		return WorldState::Name;
	}

	bool WorldState::OnMouseDown(MouseButton button, int32 x, int32 y)
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

		return true;
	}

	bool WorldState::OnMouseUp(MouseButton button, int32 x, int32 y)
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

		return true;
	}

	bool WorldState::OnMouseMove(int32 x, int32 y)
	{
		if (!m_leftButtonDown)
		{
			return false;
		}

		const Point position(x, y);
		const Point delta = position - m_lastMousePosition;
		m_lastMousePosition = position;

		if (delta.x != 0.0f)
		{
			m_cameraAnchorNode->Yaw(Degree(delta.x * s_mouseSensitivityCVar->GetFloatValue()), TransformSpace::World);
		}
		
		if (delta.y != 0.0f)
		{
			const float factor = s_invertVMouseCVar->GetBoolValue() ? -1.0f : 1.0f;
			m_cameraAnchorNode->Pitch(Degree(delta.y * factor * s_mouseSensitivityCVar->GetFloatValue()), TransformSpace::Local);
		}

		return true;
	}

	bool WorldState::OnKeyDown(int32 key)
	{
		switch(key)
		{
		case 0x57:
			m_movementVelocity.z = 1.0f;
			return false;
		case 0x53:
			m_movementVelocity.z = -1.0f;
			return false;
		}
		
		return true;
	}

	bool WorldState::OnKeyUp(int32 key)
	{
		switch(key)
		{
		case 0x57:
		case 0x53:
			m_movementVelocity.z = 0.0f;
			return false;
		}

		return true;
	}

	void WorldState::OnPaint()
	{
		m_playerNode->Translate(m_movementVelocity, TransformSpace::Local);

		FrameManager::Get().Draw();

		if (m_axisVisible)
		{
			RenderDebugAxis();
		}
	}

	void WorldState::SetupWorldScene()
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

		// The player node. Add the camera anchor node as child node
		m_playerNode = &m_scene.CreateSceneNode("Player");
		m_playerNode->AddChild(*m_cameraAnchorNode);

		// Add the player node to the world scene to make it visible
		m_scene.GetRootSceneNode().AddChild(*m_playerNode);

		// Create the player entity (currently using a default mesh) and attach the entity
		// to the players scene node to make it visible and movable in the world scene
		m_playerEntity = m_scene.CreateEntity("Player", "Models/Cube/Cube.hmsh");
		m_playerNode->AttachObject(*m_playerEntity);

		// Create the world grid in the scene. The world grid component will handle the rest for us
		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
	}

	void WorldState::OnRealmDisconnected()
	{
		// Trigger the lua event
		FrameManager::Get().TriggerLuaEvent("REALM_DISCONNECTED");

		// Go back to login state
		GameStateMgr::Get().SetGameState(LoginState::Name);
	}

	void WorldState::OnEnterWorldFailed(game::player_login_response::Type error)
	{
		GameStateMgr::Get().SetGameState(LoginState::Name);
	}

	void WorldState::RegisterGameplayCommands()
	{
		Console::RegisterCommand(command_names::ToggleAxis, [this](const std::string&, const std::string&)
		{
			ToggleAxisVisibility();
		}, ConsoleCommandCategory::Debug, "Toggles visibility of the axis display.");
	}

	void WorldState::RemoveGameplayCommands()
	{
		const String commandsToRemove[] = {
			command_names::ToggleAxis
		};

		for (const auto& command : commandsToRemove)
		{
			Console::UnregisterCommand(command);	
		}
	}

	void WorldState::ToggleAxisVisibility()
	{
		m_axisVisible = !m_axisVisible;
		if (m_axisVisible)
		{
			EnsureDebugAxisCreated();

			ILOG("DebugAxis visible");
		}
		else
		{
			ILOG("DebugAxis hidden");
		}
	}

	void WorldState::EnsureDebugAxisCreated()
	{
		/*if (m_debugAxis)
		{
			return;
		}

		m_debugAxis = std::make_unique<ManualRenderObject>(GraphicsDevice::Get());

		const auto operation = m_debugAxis->AddLineListOperation();

		auto& xLine = operation->AddLine(Vector3::Zero, Vector3::UnitX);
		xLine.SetColor(Color(1.0f, 0.0f, 0.0f));

		auto& yLine = operation->AddLine(Vector3::Zero, Vector3::UnitY);
		yLine.SetColor(Color(0.0f, 1.0f, 0.0f));

		auto& zLine = operation->AddLine(Vector3::Zero, Vector3::UnitZ);
		zLine.SetColor(Color(0.0f, 0.0f, 1.0f));*/
	}

	void WorldState::RenderDebugAxis()
	{
		/*int32 x, y, w, h;
		GraphicsDevice::Get().GetViewport(&x, &y, &w, &h);

		const Vector3 cameraDirection = m_defaultCamera->GetDerivedOrientation() * (Vector3::UnitZ * -1.0f);

		Matrix4 worldMatrix;
		worldMatrix.MakeTrans(m_defaultCamera->GetDerivedPosition() + cameraDirection * 5.0f);

		GraphicsDevice::Get().SetTransformMatrix(World, worldMatrix);
		GraphicsDevice::Get().SetTransformMatrix(View, m_defaultCamera->GetViewMatrix());
		GraphicsDevice::Get().SetTransformMatrix(Projection, m_defaultCamera->GetProjectionMatrix());

		m_debugAxis->Render();*/
	}
}
