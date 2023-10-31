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

#include "world_deserializer.h"
#include "game/chat_type.h"

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
		static const char* s_sendChatMessage = "SendChatMessage";
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
		m_paintLayer = Screen::AddLayer([this] { OnPaint(); }, 1.0f, ScreenLayerFlags::IdentityTransform);

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

		m_worldRootNode = m_scene.GetRootSceneNode().CreateChildSceneNode();
		LoadMap("Worlds/Development/Development.hwld");
	}

	void WorldState::OnLeave()
	{
		m_worldInstance.reset();

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
		// Enter
		if (key == 13)
		{
			FrameManager::Get().TriggerLuaEvent("OPEN_CHAT");
		}

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
	}

	void WorldState::SetupPacketHandler()
	{
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::UpdateObject, *this, &WorldState::OnUpdateObject);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::CompressedUpdateObject, *this, &WorldState::OnCompressedUpdateObject);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::DestroyObjects, *this, &WorldState::OnDestroyObjects);
		
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveStartForward, *this, &WorldState::OnMovement);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveStartBackward, *this, &WorldState::OnMovement);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveStop, *this, &WorldState::OnMovement);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveStartStrafeLeft, *this, &WorldState::OnMovement);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveStartStrafeRight, *this, &WorldState::OnMovement);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveStopStrafe, *this, &WorldState::OnMovement);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveStartTurnLeft, *this, &WorldState::OnMovement);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveStartTurnRight, *this, &WorldState::OnMovement);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveStopTurn, *this, &WorldState::OnMovement);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveHeartBeat, *this, &WorldState::OnMovement);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::MoveSetFacing, *this, &WorldState::OnMovement);

		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::ChatMessage, *this, &WorldState::OnChatMessage);
	}

	void WorldState::RemovePacketHandler() const
	{
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::UpdateObject);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::CompressedUpdateObject);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::DestroyObjects);

		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveStartForward);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveStartBackward);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveStop);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveStartStrafeLeft);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveStartStrafeRight);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveStopStrafe);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveStartTurnLeft);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveStartTurnRight);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveStopTurn);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveHeartBeat);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::MoveSetFacing);

		m_realmConnector.ClearPacketHandler(game::realm_client_packet::ChatMessage);
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

		Console::RegisterCommand(command_names::s_sendChatMessage, [this](const std::string& command, const std::string& args)
		{
			m_realmConnector.sendSinglePacket([this, &args](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::ChatMessage);
				packet
					<< io::write<uint8>(ChatType::Say)
					<< io::write_range(args) << io::write<uint8>(0);
				packet.Finish();
			});
		}, ConsoleCommandCategory::Debug, "Sends an ingame chat message.");
	}

	void WorldState::RemoveGameplayCommands()
	{
		const String commandsToRemove[] = {
			command_names::s_toggleAxis,
			command_names::s_toggleGrid,
			command_names::s_toggleWire,
			command_names::s_sendChatMessage
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

	PacketParseResult WorldState::OnMovement(game::IncomingPacket& packet)
	{
		uint64 characterGuid;
		MovementInfo movementInfo;
		if (!(packet >> io::read<uint64>(characterGuid) >> movementInfo))
		{
			return PacketParseResult::Disconnect;
		}
		
		const auto objectIt = m_gameObjectsById.find(characterGuid);
		if (objectIt == m_gameObjectsById.end())
		{
			WLOG("Received movement packet for unknown unit " << log_hex_digit(characterGuid));
			return PacketParseResult::Pass;
		}

		auto unitPtr = std::static_pointer_cast<GameUnitC>(objectIt->second);
		ASSERT(unitPtr);

		// Instantly apply movement data for now
		unitPtr->GetSceneNode()->SetDerivedPosition(movementInfo.position);
		unitPtr->GetSceneNode()->SetDerivedOrientation(Quaternion(Radian(movementInfo.facing), Vector3::UnitY));

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnChatMessage(game::IncomingPacket& packet)
	{
		uint64 characterGuid;
		ChatType type;
		String message;
		uint8 flags;
		if (!(packet 
			>> io::read_packed_guid(characterGuid)
			>> io::read<uint8>(type)
			>> io::read_limited_string<512>(message)
			>> io::read<uint8>(flags)))
		{
			return PacketParseResult::Disconnect;
		}

		switch(type)
		{
		case ChatType::Say:
			DLOG("[" << log_hex_digit(characterGuid) << "] says: " << message);
			break;
		case ChatType::Yell:
			DLOG("[" << log_hex_digit(characterGuid) << "] yells: " << message);
			break;
		case ChatType::Emote:
			DLOG("[" << log_hex_digit(characterGuid) << "] " << message);
			break;
		}

		FrameManager::Get().TriggerLuaEvent("CHAT_MSG_SAY", characterGuid, message);

		return PacketParseResult::Pass;
	}

	bool WorldState::LoadMap(const String& assetPath)
	{
		m_worldInstance.reset();
		m_worldInstance = std::make_unique<ClientWorldInstance>(m_scene, *m_worldRootNode);

		// TODO: Load map file
		const std::unique_ptr<std::istream> streamPtr = AssetRegistry::OpenFile(assetPath);
		if (!streamPtr)
		{
			ELOG("Failed to load world file '" << assetPath << "'");
			return false;
		}

		io::StreamSource source{ *streamPtr };
		io::Reader reader{ source };

		ClientWorldInstanceDeserializer deserializer{ *m_worldInstance };
		if (!deserializer.Read(reader))
		{
			ELOG("Failed to read world '" << assetPath << "'!");
			return false;
		}

		return true;
	}

	void WorldState::CreateMapEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale)
	{
		const String uniqueId = "Entity_" + std::to_string(m_objectIdGenerator.GenerateId());
		Entity* entity = m_scene.CreateEntity(uniqueId, assetName);
		if (entity)
		{
			auto& node = m_scene.CreateSceneNode(uniqueId);
			m_scene.GetRootSceneNode().AddChild(node);
			node.AttachObject(*entity);
			node.SetPosition(position);
			node.SetOrientation(orientation);
			node.SetScale(scale);
		}
	}
}
