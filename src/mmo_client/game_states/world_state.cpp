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
#include "game/object_type_id.h"
#include "scene_graph/entity.h"

#include <zstr/zstr.hpp>

#include "game_client/object_mgr.h"
#include "spell_projectile.h"
#include "world_deserializer.h"
#include "base/erase_by_move.h"
#include "base/timer_queue.h"
#include "game/auto_attack.h"
#include "game/chat_type.h"
#include "game_client/game_player_c.h"
#include "game/spell_target_map.h"

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
		static const char* s_freezeCulling = "ToggleCullingFreeze";
	}
	
	WorldState::WorldState(GameStateMgr& gameStateManager, RealmConnector& realmConnector, const proto_client::Project& project, TimerQueue& timers)
		: GameState(gameStateManager)
		, m_realmConnector(realmConnector)
		, m_playerNameCache(m_realmConnector)
		, m_project(project)
		, m_timers(timers)
	{
	}

	void WorldState::OnEnter()
	{
		ObjectMgr::Initialize();

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
		m_spellProjectiles.clear();

		ObjectMgr::Initialize();

		m_worldInstance.reset();

		RemovePacketHandler();

		RemoveGameplayCommands();

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

		ObjectMgr::UpdateObjects(deltaSeconds);

		// Update projectiles
		auto it = m_spellProjectiles.begin();
		while(it != m_spellProjectiles.end())
		{
			const auto& projectile = *it;
			projectile->Update(deltaSeconds);

			if (projectile->HasHit())
			{
				it = EraseByMove(m_spellProjectiles, it);
			}
			else
			{
				++it;
			}
		}

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
		m_cloudsEntity->SetQueryFlags(0);
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
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::NameQueryResult, *this, &WorldState::OnNameQueryResult);

		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::InitialSpells, *this, &WorldState::OnInitialSpells);

		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::CreatureMove, *this, &WorldState::OnCreatureMove);

		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::LearnedSpell, *this, &WorldState::OnSpellLearnedOrUnlearned);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::UnlearnedSpell, *this, &WorldState::OnSpellLearnedOrUnlearned);
		
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::SpellStart, *this, &WorldState::OnSpellStart);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::SpellGo, *this, &WorldState::OnSpellGo);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::SpellFailure, *this, &WorldState::OnSpellFailure);

		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::AttackStart, *this, &WorldState::OnAttackStart);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::AttackStop, *this, &WorldState::OnAttackStop);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::AttackSwingError, *this, &WorldState::OnAttackSwingError);

		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::XpLog, *this, &WorldState::OnXpLog);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::SpellDamageLog, *this, &WorldState::OnSpellDamageLog);
		m_realmConnector.RegisterPacketHandler(game::realm_client_packet::NonSpellDamageLog, *this, &WorldState::OnNonSpellDamageLog);

#ifdef MMO_WITH_DEV_COMMANDS
		Console::RegisterCommand("createmonster", [this](const std::string& cmd, const std::string& args) { Command_CreateMonster(cmd, args); }, ConsoleCommandCategory::Gm, "Spawns a monster from a specific id. The monster will not persist on server restart.");
		Console::RegisterCommand("destroymonster", [this](const std::string& cmd, const std::string& args) { Command_DestroyMonster(cmd, args); }, ConsoleCommandCategory::Gm, "Destroys a spawned monster from a specific guid.");
		Console::RegisterCommand("learnspell", [this](const std::string& cmd, const std::string& args) { Command_LearnSpell(cmd, args); }, ConsoleCommandCategory::Gm, "Makes the selected player learn a given spell.");
		Console::RegisterCommand("followme", [this](const std::string& cmd, const std::string& args) { Command_FollowMe(cmd, args); }, ConsoleCommandCategory::Gm, "Makes the selected creature follow you.");
		Console::RegisterCommand("faceme", [this](const std::string& cmd, const std::string& args) { Command_FaceMe(cmd, args); }, ConsoleCommandCategory::Gm, "Makes the selected creature face towards you.");
#endif

		Console::RegisterCommand("cast", [this](const std::string& cmd, const std::string& args) { Command_CastSpell(cmd, args); }, ConsoleCommandCategory::Game, "Casts a given spell.");
		Console::RegisterCommand("startattack", [this](const std::string& cmd, const std::string& args) { Command_StartAttack(cmd, args); }, ConsoleCommandCategory::Game, "Starts attacking the current target.");
	}

	void WorldState::RemovePacketHandler() const
	{
#ifdef MMO_WITH_DEV_COMMANDS
		Console::UnregisterCommand("createmonster");
		Console::UnregisterCommand("destroymonster");
		Console::UnregisterCommand("learnspell");
		Console::UnregisterCommand("followme");
		Console::UnregisterCommand("faceme");
#endif

		Console::UnregisterCommand("cast");
		Console::UnregisterCommand("startattack");

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
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::NameQueryResult);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::InitialSpells);

		m_realmConnector.ClearPacketHandler(game::realm_client_packet::CreatureMove);

		m_realmConnector.ClearPacketHandler(game::realm_client_packet::LearnedSpell);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::UnlearnedSpell);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::SpellStart);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::SpellGo);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::SpellFailure);

		m_realmConnector.ClearPacketHandler(game::realm_client_packet::AttackStart);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::AttackStop);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::AttackSwingError);

		m_realmConnector.ClearPacketHandler(game::realm_client_packet::XpLog);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::SpellDamageLog);
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::NonSpellDamageLog);

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

		Console::RegisterCommand(command_names::s_freezeCulling, [this](const std::string&, const std::string&)
			{
				// Ensure that the frustum planes are recalculated to immediately see the effect
				m_playerController->GetCamera().InvalidateView();

				// Toggle culling freeze and log current culling state
				m_scene.FreezeRendering(!m_scene.IsRenderingFrozen());
				ILOG(m_scene.IsRenderingFrozen() ? "Culling is now frozen" : "Culling is no longer frozen");
			}, ConsoleCommandCategory::Debug, "Toggles culling.");

	}

	void WorldState::RemoveGameplayCommands()
	{
		const String commandsToRemove[] = {
			command_names::s_toggleAxis,
			command_names::s_toggleGrid,
			command_names::s_toggleWire,
			command_names::s_sendChatMessage,
			command_names::s_freezeCulling
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

			bool creation = false;
			ObjectTypeId typeId;
			if (!(packet 
				>> io::read<uint8>(typeId)
				>> io::read<uint8>(creation)))
			{
				return PacketParseResult::Disconnect;
			}

			if (creation)
			{
				// Create game object from deserialization
				std::shared_ptr<GameObjectC> object;

				switch(typeId)
				{
				case ObjectTypeId::Unit:
					object = std::make_shared<GameUnitC>(m_scene, *this);
					break;
				case ObjectTypeId::Player:
					object = std::make_shared<GamePlayerC>(m_scene, *this);
					break;
				default:
					object = std::make_shared<GameObjectC>(m_scene);
				}

				object->InitializeFieldMap();
				object->Deserialize(packet, creation);

				ObjectMgr::AddObject(object);

				// TODO: Don't do it like this, add a special flag to the update object to tell that this is our controlled object!
				if (!m_playerController->GetControlledUnit())
				{
					ObjectMgr::SetActivePlayer(object->GetGuid());

					// Register player observers
					m_playerObservers += object->fieldsChanged.connect([this](uint64 guid, uint16 fieldIndex, uint16 fieldCount)
						{
							// Watch for target unit change
							if (fieldIndex <= object_fields::TargetUnit &&
								fieldIndex + fieldCount >= object_fields::TargetUnit + 1)
							{
								if (ObjectMgr::GetActivePlayerGuid() == guid)
								{
									FrameManager::Get().TriggerLuaEvent("PLAYER_TARGET_CHANGED");

									m_targetObservers.disconnect();

									auto targetUnit = ObjectMgr::Get<GameUnitC>(ObjectMgr::GetActivePlayer()->Get<uint64>(object_fields::TargetUnit));
									if (targetUnit)
									{
										targetUnit->fieldsChanged.connect([this](uint64, uint16, uint16)
											{
												FrameManager::Get().TriggerLuaEvent("PLAYER_TARGET_CHANGED");
											});
									}
								}
							}

							if ((fieldIndex <= object_fields::Xp && fieldIndex + fieldCount >= object_fields::Xp) ||
								(fieldIndex <= object_fields::NextLevelXp && fieldIndex + fieldCount >= object_fields::NextLevelXp))
							{
								FrameManager::Get().TriggerLuaEvent("PLAYER_XP_CHANGED");
							}

							if (fieldIndex <= object_fields::Level && fieldIndex + fieldCount >= object_fields::Level)
							{
								FrameManager::Get().TriggerLuaEvent("PLAYER_LEVEL_CHANGED");
							}

							if ((fieldIndex <= object_fields::Health && fieldIndex + fieldCount >= object_fields::Health) ||
								(fieldIndex <= object_fields::MaxHealth && fieldIndex + fieldCount >= object_fields::MaxHealth))
							{
								FrameManager::Get().TriggerLuaEvent("PLAYER_HEALTH_CHANGED");
							}

							if ((fieldIndex <= object_fields::Mana && fieldIndex + fieldCount >= object_fields::Mana) ||
								(fieldIndex <= object_fields::MaxMana && fieldIndex + fieldCount >= object_fields::MaxMana))
							{
								FrameManager::Get().TriggerLuaEvent("PLAYER_POWER_CHANGED");
							}
						});

					m_playerController->SetControlledUnit(std::dynamic_pointer_cast<GameUnitC>(object));
					FrameManager::Get().TriggerLuaEvent("PLAYER_ENTER_WORLD");
				}
			}
			else
			{
				uint64 guid;
				if (!(packet >> io::read_packed_guid(guid)))
				{
					return PacketParseResult::Disconnect;
				}

				const auto obj = ObjectMgr::Get<GameObjectC>(guid);
				if (!obj)
				{
					return PacketParseResult::Disconnect;
				}

				obj->Deserialize(packet, creation);

				if (obj->Get<uint32>(object_fields::Type) == static_cast<uint32>(ObjectTypeId::Unit))
				{
					if (const auto unit = std::dynamic_pointer_cast<GameUnitC>(obj))
					{
						if (const uint64 targetGuid = unit->Get<uint64>(object_fields::TargetUnit); targetGuid != 0)
						{
							if (auto targetUnit = ObjectMgr::Get<GameUnitC>(targetGuid))
							{
								unit->SetTargetUnit(targetUnit);
							}
						}
					}
				}
			}
			
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
			ObjectMgr::RemoveObject(id);
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
		
		const auto unitPtr = ObjectMgr::Get<GameUnitC>(characterGuid);
		if (!unitPtr)
		{
			WLOG("Received movement packet for unknown unit " << log_hex_digit(characterGuid));
			return PacketParseResult::Pass;
		}

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

		m_playerNameCache.Get(characterGuid, [this, type, message, flags](uint64, const String& name) 
		{
			FrameManager::Get().TriggerLuaEvent("CHAT_MSG_SAY", name, message);
		});

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnNameQueryResult(game::IncomingPacket& packet)
	{
		uint64 guid;
		bool succeeded;
		String name;
		if (!(packet
			>> io::read_packed_guid(guid)
			>> io::read<uint8>(succeeded)
			>> io::read_string(name)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!succeeded)
		{
			ELOG("Unable to retrieve unit name for unit " << log_hex_digit(guid));
			return PacketParseResult::Pass;
		}

		m_playerNameCache.NotifyObjectResponse(guid, std::move(name));
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnInitialSpells(game::IncomingPacket& packet)
	{
		std::vector<uint32> spellIds;
		if (!(packet >> io::read_container<uint16>(spellIds)))
		{
			return PacketParseResult::Disconnect;
		}

		std::vector<const proto_client::SpellEntry*> spells;
		spells.reserve(spellIds.size());

		for (const auto& spellId : spellIds)
		{
			const auto* spell = m_project.spells.getById(spellId);
			if (spell)
			{
				spells.push_back(spell);
			}
			else
			{
				WLOG("Received unknown initial spell id " << spellId);
			}
		}

		ASSERT(m_playerController->GetControlledUnit());
		m_playerController->GetControlledUnit()->SetInitialSpells(spells);

		FrameManager::Get().TriggerLuaEvent("PLAYER_SPELLS_CHANGED");

		return PacketParseResult::Pass;
	}

	Vector3 UnpackMovementVector(uint32 packed, const Vector3& mid)
	{
		Vector3 p;

		// Extract the x component
		int x_diff = (packed & 0x7FF); // Extract lower 11 bits
		if (x_diff > 0x3FF) { // Handle negative values
			x_diff |= ~0x7FF; // Extend sign bit
		}
		p.x = mid.x - (x_diff * 0.25f);

		// Extract the y component
		int y_diff = ((packed >> 11) & 0x7FF); // Extract next 11 bits
		if (y_diff > 0x3FF) { // Handle negative values
			y_diff |= ~0x7FF; // Extend sign bit
		}
		p.y = mid.y - (y_diff * 0.25f);

		// Extract the z component
		int z_diff = ((packed >> 22) & 0x3FF); // Extract next 10 bits
		if (z_diff > 0x1FF) { // Handle negative values
			z_diff |= ~0x3FF; // Extend sign bit
		}
		p.z = mid.z - (z_diff * 0.25f);

		return p;
	}

	PacketParseResult WorldState::OnCreatureMove(game::IncomingPacket& packet)
	{
		std::vector<Vector3> path;

		uint64 guid;
		Vector3 startPosition;
		Vector3 endPosition;
		GameTime timestamp;
		uint32 pathSize;

		if (!(packet 
			>> io::read_packed_guid(guid)
			>> io::read<float>(startPosition.x)
			>> io::read<float>(startPosition.y)
			>> io::read<float>(startPosition.z)
			>> io::read<uint32>(timestamp)
			>> io::read<uint32>(pathSize)
			>> io::read<float>(endPosition.x)
			>> io::read<float>(endPosition.y)
			>> io::read<float>(endPosition.z)))
		{
			return PacketParseResult::Disconnect;
		}

		// Find unit by guid
		const auto unitPtr = ObjectMgr::Get<GameUnitC>(guid);
		if(!unitPtr)
		{
			// We don't know the unit
			WLOG("Received movement packet for unknown unit id " << log_hex_digit(guid));
			return PacketParseResult::Pass;
		}

		// Ensure we are at the start position
		unitPtr->GetSceneNode()->SetPosition(startPosition);

		if (pathSize > 1)
		{
			const Vector3 mid = (startPosition + endPosition) * 0.5f;

			for (uint32 i = 1; i < pathSize - 1; ++i)
			{
				uint32 packed;
				if (!(packet >> io::read<uint32>(packed)))
				{
					return PacketParseResult::Disconnect;
				}

				path.push_back(UnpackMovementVector(packed, mid));
			}
		}

		path.push_back(endPosition);
		unitPtr->SetMovementPath(path);

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellLearnedOrUnlearned(game::IncomingPacket& packet)
	{
		uint32 spellId;
		if (!(packet >> io::read<uint32>(spellId)))
		{
			return PacketParseResult::Disconnect;
		}

		const auto* spell = m_project.spells.getById(spellId);
		if (!spell)
		{
			WLOG("Unknown spell id " << spellId);
			return PacketParseResult::Pass;
		}

		ASSERT(m_playerController->GetControlledUnit());
		if (packet.GetId() == game::realm_client_packet::LearnedSpell)
		{
			m_playerController->GetControlledUnit()->LearnSpell(spell);
		}
		else
		{
			m_playerController->GetControlledUnit()->UnlearnSpell(spellId);
		}

		FrameManager::Get().TriggerLuaEvent("PLAYER_SPELLS_CHANGED");

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellStart(game::IncomingPacket& packet)
	{
		uint64 casterId;
		uint32 spellId;
		GameTime castTime;
		SpellTargetMap targetMap;

		if (!(packet 
			>> io::read_packed_guid(casterId)
			>> io::read<uint32>(spellId)
			>> io::read<GameTime>(castTime)
			>> targetMap))
		{
			return PacketParseResult::Disconnect;
		}

		const auto* spell = m_project.spells.getById(spellId);
		if (!spell)
		{
			ELOG("Unknown spell " << spellId << " was cast!");
			return PacketParseResult::Disconnect;
		}

		if (m_playerController->GetControlledUnit())
		{
			if (casterId == m_playerController->GetControlledUnit()->GetGuid())
			{
				FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_START", spell, castTime);
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellGo(game::IncomingPacket& packet)
	{
		uint64 casterId;
		uint32 spellId;
		GameTime gameTime;
		SpellTargetMap targetMap;

		if (!(packet
			>> io::read_packed_guid(casterId)
			>> io::read<uint32>(spellId)
			>> io::read<GameTime>(gameTime)
			>> targetMap))
		{
			return PacketParseResult::Disconnect;
		}

		// Get the spell
		const auto* spell = m_project.spells.getById(spellId);
		ASSERT(spell);

		// TODO: Instead of hard coding the projectile stuff in here, make it more flexible by linking some dynamic visual data stuff to spells on the client side
		if (spell->speed() > 0.0f)
		{
			// For each target in the target map
			if (targetMap.HasUnitTarget())
			{
				auto casterUnit = ObjectMgr::Get<GameUnitC>(casterId);

				const uint64 unitTargetGuid = targetMap.GetUnitTarget();
				auto targetUnit = ObjectMgr::Get<GameUnitC>(unitTargetGuid);

				if (casterUnit && targetUnit)
				{
					// Spawn projectile
					auto projectile = std::make_unique<SpellProjectile>(m_scene, *spell, casterUnit->GetSceneNode()->GetDerivedPosition(), targetUnit);
					m_spellProjectiles.push_back(std::move(projectile));
				}
			}
		}

		if (m_playerController->GetControlledUnit())
		{
			if (casterId == m_playerController->GetControlledUnit()->GetGuid())
			{
				FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FINISH", true);
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellFailure(game::IncomingPacket& packet)
	{
		static const char* s_spellCastResultStrings[] = {
			"SPELL_CAST_FAILED_AFFECTING_COMBAT",
			"SPELL_CAST_FAILED_ALREADY_AT_FULL_HEALTH",
			"SPELL_CAST_FAILED_ALREADY_AT_FULL_MANA",
			"SPELL_CAST_FAILED_ALREADY_AT_FULL_POWER",
			"SPELL_CAST_FAILED_ALREADY_BEING_TAMED",
			"SPELL_CAST_FAILED_ALREADY_HAVE_CHARM",
			"SPELL_CAST_FAILED_ALREADY_HAVE_SUMMON",
			"SPELL_CAST_FAILED_ALREADY_OPEN",
			"SPELL_CAST_FAILED_AURA_BOUNCED",
			"SPELL_CAST_FAILED_AUTOTRACK_INTERRUPTED",
			"SPELL_CAST_FAILED_BAD_IMPLICIT_TARGETS",
			"SPELL_CAST_FAILED_BAD_TARGETS",
			"SPELL_CAST_FAILED_CANT_BE_CHARMED",
			"SPELL_CAST_FAILED_CANT_BE_DISENCHANTED",
			"SPELL_CAST_FAILED_CANT_BE_DISENCHANTED_SKILL",
			"SPELL_CAST_FAILED_CANT_BE_PROSPECTED",
			"SPELL_CAST_FAILED_CANT_CAST_ON_TAPPED",
			"SPELL_CAST_FAILED_CANT_DUEL_WHILE_INVISIBLE",
			"SPELL_CAST_FAILED_CANT_DUEL_WHILE_STEALTHED",
			"SPELL_CAST_FAILED_CANT_STEALTH",
			"SPELL_CAST_FAILED_CASTER_AURASTATE",
			"SPELL_CAST_FAILED_CASTER_DEAD",
			"SPELL_CAST_FAILED_CHARMED",
			"SPELL_CAST_FAILED_CHEST_IN_USE",
			"SPELL_CAST_FAILED_CONFUSED",
			"SPELL_CAST_FAILED_DONT_REPORT",
			"SPELL_CAST_FAILED_EQUIPPED_ITEM",
			"SPELL_CAST_FAILED_EQUIPPED_ITEM_CLASS",
			"SPELL_CAST_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND",
			"SPELL_CAST_FAILED_EQUIPPED_ITEM_CLASS_OFFHAND",
			"SPELL_CAST_FAILED_ERROR",
			"SPELL_CAST_FAILED_FIZZLE",
			"SPELL_CAST_FAILED_FLEEING",
			"SPELL_CAST_FAILED_FOOD_LOW_LEVEL",
			"SPELL_CAST_FAILED_HIGH_LEVEL",
			"SPELL_CAST_FAILED_HUNGER_SATIATED",
			"SPELL_CAST_FAILED_IMMUNE",
			"SPELL_CAST_FAILED_INTERRUPTED",
			"SPELL_CAST_FAILED_INTERRUPTED_COMBAT",
			"SPELL_CAST_FAILED_ITEM_ALREADY_ENCHANTED",
			"SPELL_CAST_FAILED_ITEM_GONE",
			"SPELL_CAST_FAILED_ITEM_NOT_FOUND",
			"SPELL_CAST_FAILED_ITEM_NOT_READY",
			"SPELL_CAST_FAILED_LEVEL_REQUIREMENT",
			"SPELL_CAST_FAILED_LINE_OF_SIGHT",
			"SPELL_CAST_FAILED_LOW_LEVEL",
			"SPELL_CAST_FAILED_LOW_CAST_LEVEL",
			"SPELL_CAST_FAILED_MAINHAND_EMPTY",
			"SPELL_CAST_FAILED_MOVING",
			"SPELL_CAST_FAILED_NEED_AMMO",
			"SPELL_CAST_FAILED_NEED_AMMO_POUCH",
			"SPELL_CAST_FAILED_NEED_EXOTIC_AMMO",
			"SPELL_CAST_FAILED_NO_PATH",
			"SPELL_CAST_FAILED_NOT_BEHIND",
			"SPELL_CAST_FAILED_NOT_FISHABLE",
			"SPELL_CAST_FAILED_NOT_FLYING",
			"SPELL_CAST_FAILED_NOT_HERE",
			"SPELL_CAST_FAILED_NOT_INFRONT",
			"SPELL_CAST_FAILED_NOT_IN_CONTROL",
			"SPELL_CAST_FAILED_NOT_KNOWN",
			"SPELL_CAST_FAILED_NOT_MOUNTED",
			"SPELL_CAST_FAILED_NOT_ON_TAXI",
			"SPELL_CAST_FAILED_NOT_ON_TRANSPORT",
			"SPELL_CAST_FAILED_NOT_READY",
			"SPELL_CAST_FAILED_NOT_SHAPESHIFT",
			"SPELL_CAST_FAILED_NOT_STANDING",
			"SPELL_CAST_FAILED_NOT_TRADABLE",
			"SPELL_CAST_FAILED_NOT_TRADING",
			"SPELL_CAST_FAILED_NOT_UNSHEATHED",
			"SPELL_CAST_FAILED_NOT_WHILE_GHOST",
			"SPELL_CAST_FAILED_NO_AMMO",
			"SPELL_CAST_FAILED_NO_CHARGES_REMAIN",
			"SPELL_CAST_FAILED_NO_CHAMPION",
			"SPELL_CAST_FAILED_NO_COMBO_POINTS",
			"SPELL_CAST_FAILED_NO_DUELING",
			"SPELL_CAST_FAILED_NO_ENDURANCE",
			"SPELL_CAST_FAILED_NO_FISH",
			"SPELL_CAST_FAILED_NO_ITEMS_WHILE_SHAPESHIFTED",
			"SPELL_CAST_FAILED_NO_MOUNTS_ALLOWED",
			"SPELL_CAST_FAILED_NO_PET",
			"SPELL_CAST_FAILED_NO_POWER",
			"SPELL_CAST_FAILED_NOTHING_TO_DISPEL",
			"SPELL_CAST_FAILED_NOTHING_TO_STEAL",
			"SPELL_CAST_FAILED_ONLY_ABOVE_WATER",
			"SPELL_CAST_FAILED_ONLY_DAYTIME",
			"SPELL_CAST_FAILED_ONLY_INDOORS",
			"SPELL_CAST_FAILED_ONLY_MOUNTED",
			"SPELL_CAST_FAILED_ONLY_NIGHTTIME",
			"SPELL_CAST_FAILED_ONLY_OUTDOORS",
			"SPELL_CAST_FAILED_ONLY_SHAPESHIFTED",
			"SPELL_CAST_FAILED_ONLY_STEALTHED",
			"SPELL_CAST_FAILED_ONLY_UNDERWATER",
			"SPELL_CAST_FAILED_OUT_OF_RANGE",
			"SPELL_CAST_FAILED_PACIFIED",
			"SPELL_CAST_FAILED_POSSESSED",
			"SPELL_CAST_FAILED_REAGENTS",
			"SPELL_CAST_FAILED_REQUIRES_AREA",
			"SPELL_CAST_FAILED_REQUIRES_SPELL_FOCUS",
			"SPELL_CAST_FAILED_ROOTED",
			"SPELL_CAST_FAILED_SILENCED",
			"SPELL_CAST_FAILED_SPELL_IN_PROGRESS",
			"SPELL_CAST_FAILED_SELL_LEARNED",
			"SPELL_CAST_FAILED_SPELL_UNAVAILABLE",
			"SPELL_CAST_FAILED_STUNNED",
			"SPELL_CAST_FAILED_TARGETS_DEAD",
			"SPELL_CAST_FAILED_TARGET_AFFECTING_COMBAT",
			"SPELL_CAST_FAILED_TARGET_AURA_STATE",
			"SPELL_CAST_FAILED_TARGET_DUELING",
			"SPELL_CAST_FAILED_TARGET_ENEMY",
			"SPELL_CAST_FAILED_TARGET_ENRAGED",
			"SPELL_CAST_FAILED_TARGET_FRIENDLY",
			"SPELL_CAST_FAILED_TARGET_IN_COMBAT",
			"SPELL_CAST_FAILED_TARGET_IS_PLAYER",
			"SPELL_CAST_FAILED_TARGET_IS_PLAYER_CONTROLLED",
			"SPELL_CAST_FAILED_TARGET_NOT_DEAD",
			"SPELL_CAST_FAILED_TARGET_NOT_IN_PARTY",
			"SPELL_CAST_FAILED_TARGET_NOT_LOOTED",
			"SPELL_CAST_FAILED_TARGET_NOT_PLAYER",
			"SPELL_CAST_FAILED_TARGET_NO_POCKETS",
			"SPELL_CAST_FAILED_TARGET_NO_WEAPONS",
			"SPELL_CAST_FAILED_TARGET_UNSKINNABLE",
			"SPELL_CAST_FAILED_THIRST_SATISFIED",
			"SPELL_CAST_FAILED_TOO_CLOSE",
			"SPELL_CAST_FAILED_TOO_MANY_OF_ITEM",
			"SPELL_CAST_FAILED_TOTEM_CATEGORY",
			"SPELL_CAST_FAILED_TOTEMS",
			"SPELL_CAST_FAILED_TRAINING_POINTS",
			"SPELL_CAST_FAILED_TRY_AGAIN",
			"SPELL_CAST_FAILED_UNIT_NOT_BEHIND",
			"SPELL_CAST_FAILED_UNIT_NOT_INFRONT",
			"SPELL_CAST_FAILED_WRONG_PET_FOOD",
			"SPELL_CAST_FAILED_NOT_WHILE_FATIGUED",
			"SPELL_CAST_FAILED_TARGET_NOT_IN_INSTANCE",
			"SPELL_CAST_FAILED_NOT_WHILE_TRADING",
			"SPELL_CAST_FAILED_TARGET_NOT_IN_RAID",
			"SPELL_CAST_FAILED_DISENCHANT_WHILE_LOOTING",
			"SPELL_CAST_FAILED_PROSPECT_WHILE_LOOTING",
			"SPELL_CAST_FAILED_PROSPECT_NEED_MORE",
			"SPELL_CAST_FAILED_TARGET_FREE_FOR_ALL",
			"SPELL_CAST_FAILED_NO_EDIBLE_CORPSES",
			"SPELL_CAST_FAILED_ONLY_BATTLEGROUNDS",
			"SPELL_CAST_FAILED_TARGET_NOT_GHOSTS",
			"SPELL_CAST_FAILED_TOO_MANY_SKILLS",
			"SPELL_CAST_FAILED_TRANSFORM_UNUSABLE",
			"SPELL_CAST_FAILED_WRONG_WEATHER",
			"SPELL_CAST_FAILED_DAMAGE_IMMUNE",
			"SPELL_CAST_FAILED_PREVENTED_BY_MECHANIC",
			"SPELL_CAST_FAILED_PLAY_TIME",
			"SPELL_CAST_FAILED_REPUTATION",
			"SPELL_CAST_FAILED_MIN_SKILL",
			"SPELL_CAST_FAILED_NOT_IN_ARENA",
			"SPELL_CAST_FAILED_NOT_ON_SHAPESHIFTED",
			"SPELL_CAST_FAILED_NOT_ON_STEALTHED",
			"SPELL_CAST_FAILED_NOT_ON_DAMAGE_IMMUNE",
			"SPELL_CAST_FAILED_NOT_ON_MOUNTED",
			"SPELL_CAST_FAILED_TOO_SHALLOW",
			"SPELL_CAST_FAILED_TARGET_NOT_IN_SANCTUARY",
			"SPELL_CAST_FAILED_TARGET_IS_TRIVIAL",
			"SPELL_CAST_FAILED_BM_OR_INVIS_GOD",
			"SPELL_CAST_FAILED_EXPERT_RIDING_REQUIREMENT",
			"SPELL_CAST_FAILED_ARTISAN_RIDING_REQUIREMENT",
			"SPELL_CAST_FAILED_NOT_IDLE",
			"SPELL_CAST_FAILED_NOT_INACTIVE",
			"SPELL_CAST_FAILED_PARTIAL_PLAY_TIME",
			"SPELL_CAST_FAILED_NO_PLAY_TIME",
			"SPELL_CAST_FAILED_NOT_IN_BATTLEGROUND",
			"SPELL_CAST_FAILED_ONLY_IN_ARENA",
			"SPELL_CAST_FAILED_TARGET_LOCKED_TO_RAID_INSTANCE"
		};

		static const char* s_unknown = "UNKNOWN";

		uint64 casterId;
		uint32 spellId;
		GameTime gameTime;
		uint8 result;

		if (!(packet
			>> io::read_packed_guid(casterId)
			>> io::read<uint32>(spellId)
			>> io::read<GameTime>(gameTime)
			>> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		if (m_playerController->GetControlledUnit())
		{
			if (casterId == m_playerController->GetControlledUnit()->GetGuid())
			{
				const char* errorMessage = s_unknown;
				if (result < std::size(s_spellCastResultStrings))
				{
					errorMessage = s_spellCastResultStrings[result];
				}

				FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FINISH", false);
				FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", errorMessage);
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnAttackStart(game::IncomingPacket& packet)
	{
		uint64 attackerGuid, victimGuid;
		GameTime attackTime;
		if (!(packet 
			>> io::read_packed_guid(attackerGuid)
			>> io::read_packed_guid(victimGuid)
			>> io::read<GameTime>(attackTime)))
		{
			return PacketParseResult::Disconnect;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnAttackStop(game::IncomingPacket& packet)
	{
		uint64 attackerGuid;
		GameTime attackTime;
		if (!(packet
			>> io::read_packed_guid(attackerGuid)
			>> io::read<GameTime>(attackTime)))
		{
			return PacketParseResult::Disconnect;
		}

		if (m_playerController->GetControlledUnit())
		{
			if (attackerGuid == m_playerController->GetControlledUnit()->GetGuid())
			{
				m_lastAttackSwingEvent = AttackSwingEvent::Unknown;
				FrameManager::Get().TriggerLuaEvent("PLAYER_ATTACK_STOP");
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnAttackSwingError(game::IncomingPacket& packet)
	{
		AttackSwingEvent attackSwingError;

		if (!(packet
			>> io::read<uint32>(attackSwingError)))
		{
			return PacketParseResult::Disconnect;
		}

		m_lastAttackSwingEvent = attackSwingError;
		OnAttackSwingErrorTimer();

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnXpLog(game::IncomingPacket& packet)
	{


		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellDamageLog(game::IncomingPacket& packet)
	{
		uint64 targetGuid;
		uint32 amount;
		SpellSchool school;
		uint8 flags;
		uint32 spellId;

		if (!(packet
			>> io::read_packed_guid(targetGuid)
			>> io::read<uint32>(spellId)
			>> io::read<uint32>(amount)
			>> io::read<uint8>(school)
			>> io::read<uint8>(flags)))
		{
			return PacketParseResult::Disconnect;
		}

		String spellName = "Unknown";
		if (const auto* spell = m_project.spells.getById(spellId))
		{
			spellName = spell->name();
			if (spell->rank() > 0)
			{
				spellName += " (Rank " + std::to_string(spell->rank()) + ")";
			}
		}

		String damageSchoolNameString;
		switch(school)
		{
		case SpellSchool::Arcane:
			damageSchoolNameString = "Arcane";
			break;
		case spell_school::Fire:
			damageSchoolNameString = "Fire";
			break;
		case spell_school::Frost:
			damageSchoolNameString = "Frost";
			break;
		case spell_school::Holy:
			damageSchoolNameString = "Holy";
			break;
		case spell_school::Nature:
			damageSchoolNameString = "Nature";
			break;
		case spell_school::Shadow:
			damageSchoolNameString = "Shadow";
			break;
		case spell_school::Normal:
			damageSchoolNameString = "Physical";
			break;
		}

		DLOG("Spell '" << spellName << "' dealed " << amount << " " << damageSchoolNameString << " damage to target " << log_hex_digit(targetGuid));

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnNonSpellDamageLog(game::IncomingPacket& packet)
	{
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnLogEnvironmentalDamage(game::IncomingPacket& packet)
	{
		return PacketParseResult::Pass;
	}

	void WorldState::Command_LearnSpell(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		if (tokens.size() != 1)
		{
			ELOG("Usage: learnspell <entry>");
			return;
		}

		const uint32 entry = std::stoul(tokens[0]);
		if (entry == 0)
		{
			ELOG("Invalid spell id provided: '" << entry << "'");
			return;
		}

		m_realmConnector.LearnSpell(entry);
	}

	void WorldState::Command_CreateMonster(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		if (tokens.size() != 1)
		{
			ELOG("Usage: createmonster <entry>");
			return;
		}

		const uint32 entry = std::stoul(tokens[0]);
		m_realmConnector.CreateMonster(entry);
	}

	void WorldState::Command_DestroyMonster(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		if (tokens.size() > 1)
		{
			ELOG("Usage: destroymonster <entry>");
			return;
		}

		uint64 guid = 0;
		if (tokens.empty())
		{
			guid = m_playerController->GetControlledUnit()->Get<uint64>(object_fields::TargetUnit);
		}
		else
		{
			guid = std::stoul(tokens[0]);
		}

		if (guid == 0)
		{
			ELOG("No target selected and no target guid provided to destroy!");
			return;
		}

		m_realmConnector.DestroyMonster(guid);
	}

	void WorldState::Command_FaceMe(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		const uint64 guid = m_playerController->GetControlledUnit()->Get<uint64>(object_fields::TargetUnit);
		if (guid == 0)
		{
			ELOG("No target selected and no target guid provided to destroy!");
			return;
		}

		m_realmConnector.FaceMe(guid);
	}

	void WorldState::Command_FollowMe(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		const uint64 guid = m_playerController->GetControlledUnit()->Get<uint64>(object_fields::TargetUnit);
		if (guid == 0)
		{
			ELOG("No target selected and no target guid provided to destroy!");
			return;
		}

		m_realmConnector.FollowMe(guid);
	}

	void WorldState::Command_CastSpell(const std::string& cmd, const std::string& args)
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		if (tokens.size() != 1)
		{
			ELOG("Usage: cast <spellId>");
			return;
		}

		auto unit = m_playerController->GetControlledUnit();
		if (!unit)
		{
			return;
		}

		const uint32 entry = std::stoul(tokens[0]);
		const uint64 targetUnitGuid = unit->Get<uint64>(object_fields::TargetUnit);

		SpellTargetMap targetMap{};
		if (targetUnitGuid != 0)
		{
			targetMap.SetTargetMap(spell_cast_target_flags::Unit);
			targetMap.SetUnitTarget(targetUnitGuid);
		}
		else
		{
			targetMap.SetTargetMap(spell_cast_target_flags::Self);
			targetMap.SetUnitTarget(unit->GetGuid());
		}
		
		m_realmConnector.CastSpell(entry, targetMap);
	}

	void WorldState::Command_StartAttack(const std::string& cmd, const std::string& args)
	{
		auto unit = m_playerController->GetControlledUnit();
		if (!unit)
		{
			return;
		}

		uint64 targetGuid = unit->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			ELOG("No target to attack");
			return;
		}

		auto targetUnit = ObjectMgr::Get<GameUnitC>(targetGuid);
		if (!targetUnit)
		{
			ELOG("Target unit not found!");
			return;
		}

		unit->Attack(*targetUnit);
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

	void WorldState::OnChatNameQueryCallback(uint64 guid, const String& name)
	{
		FrameManager::Get().TriggerLuaEvent("CHAT_MSG_SAY", name);
	}

	void WorldState::OnAttackSwingErrorTimer()
	{
		// Do we need to continue showing the last attack swing error event?
		if (m_lastAttackSwingEvent == attack_swing_event::Success ||
			m_lastAttackSwingEvent == attack_swing_event::Unknown)
		{
			return;
		}

		String errorEvent;
		switch (m_lastAttackSwingEvent)
		{
		case attack_swing_event::CantAttack:
			errorEvent = "ATTACK_SWING_CANT_ATTACK";
			break;
		case attack_swing_event::TargetDead:
			errorEvent = "ATTACK_SWING_TARGET_DEAD";
			break;
		case attack_swing_event::WrongFacing:
			errorEvent = "ATTACK_SWING_WRONG_FACING";
			break;
		case attack_swing_event::NotStanding:
			errorEvent = "ATTACK_SWING_NOT_STANDING";
			break;
		case attack_swing_event::OutOfRange:
			errorEvent = "ATTACK_SWING_OUT_OF_RANGE";
			break;
		default:
			errorEvent = "UNKNOWN";
			break;
		}

		FrameManager::Get().TriggerLuaEvent("ATTACK_SWING_ERROR", errorEvent);

		EnqueueNextAttackSwingTimer();
	}

	void WorldState::EnqueueNextAttackSwingTimer()
	{
		m_timers.AddEvent([this]()
			{
				OnAttackSwingErrorTimer();
			}, GetAsyncTimeMs() + 500);
	}

	void WorldState::SendAttackStart(const uint64 victim, const GameTime timestamp)
	{
		m_realmConnector.sendSinglePacket([victim, timestamp](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::AttackSwing);
			packet << io::write_packed_guid(victim) << io::write<GameTime>(timestamp);
			packet.Finish();
		});
	}

	void WorldState::SendAttackStop(const GameTime timestamp)
	{
		m_realmConnector.sendSinglePacket([timestamp](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::AttackStop);
			packet << io::write<GameTime>(timestamp);
			packet.Finish();
		});
	}
}
