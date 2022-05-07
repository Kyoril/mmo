// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "world_state.h"
#include "client.h"

#include "event_loop.h"
#include "game_state_mgr.h"
#include "console/console.h"
#include "game_states/login_state.h"
#include "net/realm_connector.h"
#include "ui/world_frame.h"
#include "ui/world_renderer.h"

#include "assets/asset_registry.h"
#include "frame_ui/frame_mgr.h"
#include "game/field_map.h"
#include "game/object_type_id.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_manager.h"

#include <zstr/zstr.hpp>

namespace mmo
{
	const std::string WorldState::Name = "world";

	extern CharacterView s_selectedCharacter;

	// Console command names
	namespace command_names
	{
		static const char* s_toggleAxis = "ToggleAxis";
		static const char* s_toggleGrid = "ToggleGrid";
		static const char* s_toggleWire = "ToggleWire";
	}
	
	WorldState::WorldState(GameStateMgr& gameStateManager, RealmConnector& realmConnector)
		: GameState(gameStateManager)
		, m_realmConnector(realmConnector)
	{
	}

	void WorldState::OnEnter()
	{
		SetupWorldScene();

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
			EventLoop::MouseWheel.connect(this, &WorldState::OnMouseWheel),
			EventLoop::KeyUp.connect(this, &WorldState::OnKeyUp),
			EventLoop::Idle.connect(this, &WorldState::OnIdle)
		};
		
		RegisterGameplayCommands();

		SetupPacketHandler();
	}

	void WorldState::OnLeave()
	{
		RemovePacketHandler();

		RemoveGameplayCommands();
		
		m_gameObjectsById.clear();
		m_playerController.reset();
		m_worldGrid.reset();
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

	std::string_view WorldState::GetName() const
	{
		return Name;
	}

	bool WorldState::OnMouseDown(const MouseButton button, const int32 x, const int32 y)
	{
		m_playerController->OnMouseDown(button, x, y);
		return true;
	}

	bool WorldState::OnMouseUp(const MouseButton button, const int32 x, const int32 y)
	{
		m_playerController->OnMouseUp(button, x, y);
		return true;
	}

	bool WorldState::OnMouseMove(const int32 x, const int32 y)
	{
		m_playerController->OnMouseMove(x, y);
		return true;
	}

	bool WorldState::OnKeyDown(const int32 key)
	{
		m_playerController->OnKeyDown(key);
		return true;
	}

	bool WorldState::OnKeyUp(const int32 key)
	{
		m_playerController->OnKeyUp(key);
		return true;
	}

	void WorldState::OnIdle(const float deltaSeconds, GameTime timestamp)
	{
		m_playerController->Update(deltaSeconds);

		if (m_cloudsNode && m_playerController->GetRootNode())
		{
			m_cloudsNode->SetPosition(m_playerController->GetRootNode()->GetPosition());
			m_cloudsNode->Yaw(Radian(deltaSeconds * 0.025f), TransformSpace::World);
		}
	}
	
	bool WorldState::OnMouseWheel(const int32 delta)
	{
		m_playerController->OnMouseWheel(delta);
		return true;
	}

	void WorldState::OnPaint()
	{
		FrameManager::Get().Draw();
	}

	void WorldState::SetupWorldScene()
	{
		m_cloudsEntity = m_scene.CreateEntity("Clouds", "Models/SkySphere.hmsh");
		m_cloudsEntity->SetRenderQueueGroup(SkiesEarly);
		m_cloudsNode = &m_scene.CreateSceneNode("Clouds");
		m_cloudsNode->AttachObject(*m_cloudsEntity);
		m_cloudsNode->SetScale(Vector3::UnitScale * 40.0f);
		m_scene.GetRootSceneNode().AddChild(*m_cloudsNode);

		m_groundEntity = m_scene.CreateEntity("Ground", "Models/Floor/Floor.hmsh");
		m_groundNode = &m_scene.CreateSceneNode("Ground");
		m_groundNode->AttachObject(*m_groundEntity);
		m_scene.GetRootSceneNode().AddChild(*m_groundNode);

		m_playerController = std::make_unique<PlayerController>(m_scene, m_realmConnector);

		// Create the world grid in the scene. The world grid component will handle the rest for us
		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_worldGrid->SetVisible(false);

		// Debug axis object
		m_debugAxis = std::make_unique<AxisDisplay>(m_scene, "WorldDebugAxis");
		m_scene.GetRootSceneNode().AddChild(m_debugAxis->GetSceneNode());
		m_debugAxis->SetVisible(false);

		// Setup sun light
		m_sunLight = &m_scene.CreateLight("SunLight", LightType::Directional);
		m_sunLight->SetDirection(Vector3(1.0f, -0.5f, 1.0f).NormalizedCopy());
		m_sunLight->SetPowerScale(1.0f);
		m_sunLight->SetColor(Color::White);
		m_scene.GetRootSceneNode().AttachObject(*m_sunLight);
		
		m_archEntity = m_scene.CreateEntity("FortressArch01", "Models/Fortress_Arch_01/Fortress_Arch_01.hmsh");
		m_towerLeftEntity = m_scene.CreateEntity("FortressTowerLeft01", "Models/Fortress_Tower_01/Fortress_Tower_01.hmsh");
		m_towerRightEntity = m_scene.CreateEntity("FortressTowerRight01", "Models/Fortress_Tower_01/Fortress_Tower_01.hmsh");

		m_archNode = &m_scene.CreateSceneNode("FortressArch01");
		m_scene.GetRootSceneNode().AddChild(*m_archNode);
		m_archNode->SetPosition({1.1f, 0.0f, -0.1f });
		m_archNode->AttachObject(*m_archEntity);
		
		m_towerLeftNode = &m_scene.CreateSceneNode("FortressTowerLeft01");
		m_scene.GetRootSceneNode().AddChild(*m_towerLeftNode);
		m_towerLeftNode->SetPosition({-7.45f, -0.05f, -0.2f });
		m_towerLeftNode->AttachObject(*m_towerLeftEntity);

		m_towerRightNode = &m_scene.CreateSceneNode("FortressTowerRight01");
		m_scene.GetRootSceneNode().AddChild(*m_towerRightNode);
		m_towerRightNode->SetPosition({9.55f, -0.05f, -0.2f });
		m_towerRightNode->AttachObject(*m_towerRightEntity);
		
		auto* centrailStairEntity = m_scene.CreateEntity("CentralStairs", "Models/Central_Stairs.hmsh");
		auto& centralStairNode = m_scene.CreateSceneNode("CentralStairs");
		m_scene.GetRootSceneNode().AddChild(centralStairNode);
		centralStairNode.SetOrientation(Quaternion(Degree(-180), Vector3::UnitY));
		centralStairNode.SetPosition({-0.45f, -0.10f, 2.75f + 50.0f });
		centralStairNode.AttachObject(*centrailStairEntity);

		auto* flowerBedEntity = m_scene.CreateEntity("Stone_Flower_Bed_01", "Models/Stone_Flower_Bed_01.hmsh");
		auto& flowerBedNode = m_scene.CreateSceneNode("Stone_Flower_Bed_01");
		m_scene.GetRootSceneNode().AddChild(flowerBedNode);
		flowerBedNode.SetOrientation(Quaternion(Degree(-180), Vector3::UnitY));
		flowerBedNode.SetPosition({-0.60f, -0.15f, 1.93f + 50.0f });
		flowerBedNode.AttachObject(*flowerBedEntity);
	}

	void WorldState::SetupPacketHandler()
	{
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::UpdateObject, *this, &WorldState::OnUpdateObject);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::CompressedUpdateObject, *this, &WorldState::OnCompressedUpdateObject);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::DestroyObjects, *this, &WorldState::OnDestroyObjects);
	}

	void WorldState::RemovePacketHandler() const
	{
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::UpdateObject);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::CompressedUpdateObject);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::DestroyObjects);
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

	void WorldState::RegisterGameplayCommands() const
	{
		Console::RegisterCommand(command_names::s_toggleAxis, [this](const std::string&, const std::string&)
		{
			ToggleAxisVisibility();
		}, ConsoleCommandCategory::Debug, "Toggles visibility of the axis display.");

		Console::RegisterCommand(command_names::s_toggleGrid, [this](const std::string&, const std::string&)
		{
			ToggleGridVisibility();
		}, ConsoleCommandCategory::Debug, "Toggles visibility of the world grid display.");
		
		Console::RegisterCommand(command_names::s_toggleWire, [this](const std::string&, const std::string&)
		{
			ToggleWireframe();
		}, ConsoleCommandCategory::Debug, "Toggles wireframe render mode.");
	}

	void WorldState::RemoveGameplayCommands()
	{
		const String commandsToRemove[] = {
			command_names::s_toggleAxis,
			command_names::s_toggleGrid,
			command_names::s_toggleWire
		};

		for (const auto& command : commandsToRemove)
		{
			Console::UnregisterCommand(command);	
		}
	}

	void WorldState::ToggleAxisVisibility() const
	{
		m_debugAxis->SetVisible(!m_debugAxis->IsVisible());
		if (m_debugAxis->IsVisible())
		{
			ILOG("DebugAxis visible");
		}
		else
		{
			ILOG("DebugAxis hidden");
		}
	}

	void WorldState::ToggleGridVisibility() const
	{
		m_worldGrid->SetVisible(!m_worldGrid->IsVisible());
		if (m_worldGrid->IsVisible())
		{
			ILOG("WorldGrid visible");
		}
		else
		{
			ILOG("WorldGrid hidden");
		}
	}

	void WorldState::ToggleWireframe() const
	{
		auto& camera = m_playerController->GetCamera();
		camera.SetFillMode(camera.GetFillMode() == FillMode::Solid ? FillMode::Wireframe : FillMode::Solid);
		if (camera.GetFillMode() == FillMode::Wireframe)
		{
			ILOG("Wireframe active");
		}
		else
		{
			ILOG("Wireframe inactive");
		}
	}

	PacketParseResult WorldState::OnUpdateObject(game::IncomingPacket& packet)
	{
		uint16 numObjectUpdates;
		if (!(packet >> io::read<uint16>(numObjectUpdates)))
		{
			return PacketParseResult::Disconnect;
		}

		auto result = PacketParseResult::Disconnect;
		for (auto i = 0; i < numObjectUpdates; ++i)
		{
			result = PacketParseResult::Disconnect;

			ObjectTypeId typeId;
			if (!(packet >> io::read<uint8>(typeId)))
			{
				break;
			}

			// TODO: switch typeId

			// Create game object from deserialization
			auto object = std::make_shared<GameUnitC>(m_scene);
			object->Deserialize(packet);

			// TODO: Don't do it like this, add a special flag to the update object to tell that this is our controlled object!
			if (m_gameObjectsById.empty())
			{
				m_playerController->SetControlledUnit(object);
			}

			DLOG("Spawning object guid " << log_hex_digit(object->GetGuid()));
			m_gameObjectsById[object->GetGuid()] = std::move(object);
			
			result = PacketParseResult::Pass;
		}
		
		return result;
	}
	
	PacketParseResult WorldState::OnCompressedUpdateObject(game::IncomingPacket& packet)
	{
		TODO("Implement");
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnDestroyObjects(game::IncomingPacket& packet)
	{
		uint16 objectCount = 0;
		if (!(packet >> io::read<uint16>(objectCount)))
		{
			return PacketParseResult::Disconnect;
		}
		
		for (auto i = 0; i < objectCount; ++i)
		{
			ObjectGuid id;
			if (!(packet >> io::read_packed_guid(id)))
			{
				return PacketParseResult::Disconnect;
			}
			
			if (m_playerController->GetControlledUnit() &&
				m_playerController->GetControlledUnit()->GetGuid() == id)
			{
				ELOG("Despawn of player controlled object!");
				m_playerController->SetControlledUnit(nullptr);
			}
			
			DLOG("Despawning object " << log_hex_digit(id));
			const auto objectIt = m_gameObjectsById.find(id);
			if (objectIt != m_gameObjectsById.end())
			{
				m_gameObjectsById.erase(objectIt);
			}
		}
		
		return PacketParseResult::Pass;
	}
}
