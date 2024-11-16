// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "world_state.h"
#include "client.h"
#include "loot_client.h"

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
#include "frame_ui/text_component.h"
#include "game/auto_attack.h"
#include "game/chat_type.h"
#include "game/damage_school.h"
#include "game_client/game_player_c.h"
#include "game/spell_target_map.h"
#include "game_client/game_item_c.h"
#include "ui/world_text_frame.h"
#include "game/loot.h"
#include "game_client/game_bag_c.h"

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

	namespace
	{
		String MapMouseButton(const MouseButton button)
		{
			if ((button & MouseButton::Left) == MouseButton::Left) return "LMB";
			if ((button & MouseButton::Right) == MouseButton::Right) return "RMB";
			if ((button & MouseButton::Middle) == MouseButton::Middle) return "MMB";

			return {};
		}

		String MapBindingKeyCode(const Key keyCode)
		{
			if (keyCode >= VK_F1 && keyCode <= VK_F12)
			{
				return String("F") + std::to_string(keyCode - VK_F1 + 1);
			}

			// Convert numbers and letters to string
			if (keyCode >= 'A' && keyCode <= 'Z' ||
				keyCode >= '0' && keyCode <= '9')
			{
				return String(1, static_cast<char>(keyCode));
			}

			// Convert lowercase to uppercase for simplicity
			if (keyCode >= 'a' && keyCode <= 'z')
			{
				return String(1, static_cast<char>(keyCode - 'a' + 'A'));
			}

			if (keyCode >= VK_NUMPAD0 && keyCode <= VK_NUMPAD9)
			{
				return String("NUM-") + String(1, static_cast<char>(keyCode - VK_NUMPAD0 + '0'));
			}

			switch(keyCode)
			{
			case 0x20:		return "SPACE";
			case 0x0D:		return "ENTER";
			case 0x1B:		return "ESCAPE";
			case 0x08:		return "BACKSPACE";
			case 0x09:		return "TAB";
			case 0x6B:		return "ADD";
			case 0x6D:	return "SUBTRACT";
			case 0x6A:	return "MULTIPLY";
			case 0x6F:		return "DIVIDE";
			case 0x1E:		return "ACCEPT";
			case 0x2E:		return "DEL";
			case 0x2D:		return "INSERT";
			case 0xA2:	return "LCTRL";
			case 0xA3:	return "RCTRL";
			case 0xA0:		return "LSHIFT";
			case 0xA1:		return "RSHIFT";
			case 0x25:		return "LEFT";
			case 0x27:		return "RIGHT";
			case 0x26:			return "UP";
			case 0x28:		return "DOWN";
			case 0x21:		return "PAGEUP";
			case 0x22:		return "PAGEDOWN";
			case 0x23:		return "END";
			case 0x24:		return "HOME";
			case 0x2C:		return "PRINTSCREEN";
			case 0x91:		return "SCROLLLOCK";
			case 0x13:		return "PAUSE";
			default:
				break;
			}

			return {};
		}
	}

	IInputControl* WorldState::s_inputControl = nullptr;

	WorldState::WorldState(GameStateMgr& gameStateManager, RealmConnector& realmConnector, const proto_client::Project& project, TimerQueue& timers, LootClient& lootClient, DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& itemCache)
		: GameState(gameStateManager)
		, m_realmConnector(realmConnector)
		, m_lootClient(lootClient)
		, m_playerNameCache(m_realmConnector)
		, m_creatureCache(m_realmConnector)
		, m_itemCache(itemCache)
		, m_questCache(m_realmConnector)
		, m_project(project)
		, m_timers(timers)
	{
	}

	void WorldState::OnEnter()
	{
		ObjectMgr::Initialize(m_project);

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

		// Load bindings
		m_bindings.Initialize(*m_playerController);
		m_bindings.Load("Interface/Bindings.xml");

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

		// TODO: Remove me. We abuse this here for preloading the font
		WorldTextFrame frame(m_playerController->GetCamera(), Vector3::Zero, 0.0f);
		frame.GetFont()->GetTextWidth("1");

		m_worldRootNode = m_scene.GetRootSceneNode().CreateChildSceneNode();
		LoadMap("Worlds/Development/Development");
	}

	void WorldState::OnLeave()
	{
		m_rayQuery.reset();

		// Stop background loading thread
		m_work.reset();
		m_workQueue.stop();
		m_dispatcher.stop();
		m_backgroundLoader.join();

		m_workQueue.reset();
		m_dispatcher.reset();

		m_spellProjectiles.clear();

		ObjectMgr::Initialize(m_project);

		m_worldInstance.reset();

		RemovePacketHandler();

		RemoveGameplayCommands();

		s_inputControl = nullptr;
		m_playerController.reset();
		m_worldInstance.reset();
		m_worldGrid.reset();
		m_debugAxis.reset();
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

		// Remove bindings
		m_bindings.Unload();
		m_bindings.Shutdown();
	}

	std::string_view WorldState::GetName() const
	{
		return Name;
	}

	bool WorldState::OnMouseDown(const MouseButton button, const int32 x, const int32 y)
	{
		if (m_bindings.ExecuteKey(MapMouseButton(button), BindingKeyState::Down))
		{
			return true;
		}

		m_playerController->OnMouseDown(button, x, y);
		return true;
	}

	bool WorldState::OnMouseUp(const MouseButton button, const int32 x, const int32 y)
	{
		if (m_bindings.ExecuteKey(MapMouseButton(button), BindingKeyState::Up))
		{
			return true;
		}

		m_playerController->OnMouseUp(button, x, y);
		return true;
	}

	bool WorldState::OnMouseMove(const int32 x, const int32 y)
	{
		m_playerController->OnMouseMove(x, y);
		return true;
	}

	bool WorldState::OnKeyDown(const int32 key, bool repeat)
	{
		if (m_bindings.ExecuteKey(MapBindingKeyCode(key), repeat ? BindingKeyState::Repeat : BindingKeyState::Down))
		{
			return true;
		}

		return true;
	}

	bool WorldState::OnKeyUp(const int32 key)
	{
		if (m_bindings.ExecuteKey(MapBindingKeyCode(key), BindingKeyState::Up))
		{
			return true;
		}

		return true;
	}

	void WorldState::OnIdle(const float deltaSeconds, GameTime timestamp)
	{
		m_dispatcher.poll();

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

		const auto pos = GetPagePositionFromCamera();
		m_memoryPointOfView->UpdateCenter(pos);
		m_visibleSection->UpdateCenter(pos);

		// Update world text frames
		for (size_t i = 0; i < m_worldTextFrames.size(); )
		{
			m_worldTextFrames[i]->Update(deltaSeconds);

			// Maybe delete world text frame if it expired
			if (m_worldTextFrames[i]->IsExpired())
			{
				m_worldTextFrames.erase(m_worldTextFrames.begin() + i);
			}
			else
			{
				++i;
			}
		}

#ifdef _DEBUG
		FrameManager::Get().TriggerLuaEvent("HEIGHTCHECK_PROFILE", m_rayQuery->GetDebugHitTestResults().size());
#endif
	}
	
	bool WorldState::OnMouseWheel(const int32 delta)
	{
		m_playerController->OnMouseWheel(delta);
		return true;
	}

	void WorldState::OnPaint()
	{
		FrameManager::Get().Draw();

		// Draw world text frames
		for(const auto& textFrame : m_worldTextFrames)
		{
			textFrame->Render();
		}
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

		m_rayQuery = std::move(m_scene.CreateRayQuery(Ray()));
		m_rayQuery->SetSortByDistance(true);
		m_rayQuery->SetQueryMask(1);
		m_rayQuery->SetDebugHitTestResults(true);

		m_playerController = std::make_unique<PlayerController>(m_scene, m_realmConnector, m_lootClient);
		s_inputControl = m_playerController.get();

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

		// Ensure the work queue is always busy
		m_work = std::make_unique<asio::io_service::work>(m_workQueue);

		// Setup background loading thread
		auto& workQueue = m_workQueue;
		auto& dispatcher = m_dispatcher;
		m_backgroundLoader = std::thread([&workQueue]()
			{
				workQueue.run();
			});

		const auto addWork = [&workQueue](const WorldPageLoader::Work& work)
			{
				workQueue.post(work);
			};
		const auto synchronize = [&dispatcher](const WorldPageLoader::Work& work)
			{
				dispatcher.post(work);
			};

		const PagePosition pos = GetPagePositionFromCamera();
		m_visibleSection = std::make_unique<LoadedPageSection>(pos, 1, *this);
		m_pageLoader = std::make_unique<WorldPageLoader>(*m_visibleSection, addWork, synchronize);

		PagePosition worldSize(64, 64);
		m_memoryPointOfView = std::make_unique<PagePOVPartitioner>(
			worldSize,
			2,
			pos,
			*m_pageLoader
		);
	}

	void WorldState::SetupPacketHandler()
	{
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::UpdateObject, *this, &WorldState::OnUpdateObject);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::CompressedUpdateObject, *this, &WorldState::OnCompressedUpdateObject);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::DestroyObjects, *this, &WorldState::OnDestroyObjects);
		
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStartForward, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStartBackward, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStop, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStartStrafeLeft, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStartStrafeRight, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStopStrafe, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStartTurnLeft, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStartTurnRight, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStopTurn, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveHeartBeat, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveSetFacing, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveJump, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveFallLand, *this, &WorldState::OnMovement);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ChatMessage, *this, &WorldState::OnChatMessage);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::NameQueryResult, *this, &WorldState::OnNameQueryResult);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::InitialSpells, *this, &WorldState::OnInitialSpells);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::CreatureMove, *this, &WorldState::OnCreatureMove);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::LearnedSpell, *this, &WorldState::OnSpellLearnedOrUnlearned);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::UnlearnedSpell, *this, &WorldState::OnSpellLearnedOrUnlearned);
		
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SpellStart, *this, &WorldState::OnSpellStart);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SpellGo, *this, &WorldState::OnSpellGo);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SpellFailure, *this, &WorldState::OnSpellFailure);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::AttackStart, *this, &WorldState::OnAttackStart);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::AttackStop, *this, &WorldState::OnAttackStop);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::AttackSwingError, *this, &WorldState::OnAttackSwingError);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::XpLog, *this, &WorldState::OnXpLog);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SpellDamageLog, *this, &WorldState::OnSpellDamageLog);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::NonSpellDamageLog, *this, &WorldState::OnNonSpellDamageLog);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::CreatureQueryResult, *this, &WorldState::OnCreatureQueryResult);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ItemQueryResult, *this, &WorldState::OnItemQueryResult);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::QuestQueryResult, *this, &WorldState::OnQuestQueryResult);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ForceMoveSetWalkSpeed, *this, &WorldState::OnForceMovementSpeedChange);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ForceMoveSetRunSpeed, *this, &WorldState::OnForceMovementSpeedChange);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ForceMoveSetRunBackSpeed, *this, &WorldState::OnForceMovementSpeedChange);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ForceMoveSetSwimSpeed, *this, &WorldState::OnForceMovementSpeedChange);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ForceMoveSetSwimBackSpeed, *this, &WorldState::OnForceMovementSpeedChange);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ForceMoveSetTurnRate, *this, &WorldState::OnForceMovementSpeedChange);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ForceSetFlightSpeed, *this, &WorldState::OnForceMovementSpeedChange);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ForceSetFlightBackSpeed, *this, &WorldState::OnForceMovementSpeedChange);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveTeleportAck, *this, &WorldState::OnMoveTeleport);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveSetWalkSpeed, *this, &WorldState::OnMovementSpeedChanged);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveSetRunSpeed, *this, &WorldState::OnMovementSpeedChanged);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveSetRunBackSpeed, *this, &WorldState::OnMovementSpeedChanged);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveSetSwimSpeed, *this, &WorldState::OnMovementSpeedChanged);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveSetSwimBackSpeed, *this, &WorldState::OnMovementSpeedChanged);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveSetTurnRate, *this, &WorldState::OnMovementSpeedChanged);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SetFlightSpeed, *this, &WorldState::OnMovementSpeedChanged);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SetFlightBackSpeed, *this, &WorldState::OnMovementSpeedChanged);

		m_lootClient.Initialize();

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

	void WorldState::RemovePacketHandler()
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

		m_lootClient.Shutdown();

		m_worldPacketHandlers.Clear();
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
			ELOG("Failed to read update object count!");
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
				ELOG("Failed to read object update type");
				return PacketParseResult::Disconnect;
			}

			if (creation)
			{
				// Create game object from deserialization
				std::shared_ptr<GameObjectC> object;

				switch(typeId)
				{
				case ObjectTypeId::Unit:
					object = std::make_shared<GameUnitC>(m_scene, *this, *this);
					break;
				case ObjectTypeId::Player:
					object = std::make_shared<GamePlayerC>(m_scene, *this, *this);
					break;
				case ObjectTypeId::Item:
					object = std::make_shared<GameItemC>(m_scene, *this);
					break;
				case ObjectTypeId::Container:
					object = std::make_shared<GameBagC>(m_scene, *this);
					break;
				default:
					ASSERT(!! "Unknown object type");
				}

				ASSERT(object);

				object->InitializeFieldMap();
				object->Deserialize(packet, creation);

				if (!packet)
				{
					ELOG("Failed to read object fields of object creation packet #" << i << " (Object type: " << static_cast<int32>(typeId) << ")");
					return PacketParseResult::Disconnect;
				}

				ObjectMgr::AddObject(object);

				// TODO: Don't do it like this, add a special flag to the update object to tell that this is our controlled object!
				if (!m_playerController->GetControlledUnit() && object->GetTypeId() == ObjectTypeId::Player)
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

									if (const auto targetUnit = ObjectMgr::Get<GameUnitC>(ObjectMgr::GetActivePlayer()->Get<uint64>(object_fields::TargetUnit)))
									{
										targetUnit->fieldsChanged.connect([this](uint64, uint16, uint16)
											{
												FrameManager::Get().TriggerLuaEvent("PLAYER_TARGET_CHANGED");
											});
									}
								}
							}

							if ((fieldIndex < object_fields::BankSlot_1 && fieldIndex + fieldCount >= object_fields::InvSlotHead))
							{
								FrameManager::Get().TriggerLuaEvent("INVENTORY_CHANGED");
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

								if (m_playerController->GetControlledUnit()->GetHealth() <= 0)
								{
									FrameManager::Get().TriggerLuaEvent("PLAYER_DEAD");
								}
							}

							if ((fieldIndex <= object_fields::Energy && fieldIndex + fieldCount >= object_fields::Mana) ||
								(fieldIndex <= object_fields::MaxEnergy && fieldIndex + fieldCount >= object_fields::MaxMana))
							{
								FrameManager::Get().TriggerLuaEvent("PLAYER_POWER_CHANGED");
							}
						});

					m_playerController->SetControlledUnit(std::dynamic_pointer_cast<GameUnitC>(object));
					FrameManager::Get().TriggerLuaEvent("PLAYER_ENTER_WORLD");

					if (m_playerController->GetControlledUnit()->GetHealth() <= 0)
					{
						FrameManager::Get().TriggerLuaEvent("PLAYER_DEAD");
					}
				}
			}
			else
			{
				uint64 guid;
				if (!(packet >> io::read_packed_guid(guid)))
				{
					ELOG("Failed to read object guid of object update packet #" << i);
					return PacketParseResult::Disconnect;
				}

				const auto obj = ObjectMgr::Get<GameObjectC>(guid);
				if (!obj)
				{
					ELOG("Failed to find updated object with guid " << log_hex_digit(guid));
					return PacketParseResult::Disconnect;
				}

				obj->Deserialize(packet, creation);
				if (!packet)
				{
					ELOG("Failed to read object fields of object update packet #" << i << " (Object guid: " << log_hex_digit(guid) << ")");
					return PacketParseResult::Disconnect;
				}

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

		unitPtr->ApplyMovementInfo(movementInfo);

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

		m_playerNameCache.NotifyObjectResponse(guid, name);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnCreatureQueryResult(game::IncomingPacket& packet)
	{
		uint64 id;
		bool succeeded;
		if (!(packet
			>> io::read_packed_guid(id)
			>> io::read<uint8>(succeeded)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Received creature data for entry " << id);

		if (!succeeded)
		{
			ELOG("Creature query for id " << log_hex_digit(id) << " failed");
			return PacketParseResult::Pass;
		}

		CreatureInfo entry{ id };
		if (!(packet >> io::read_string(entry.name) >> io::read_string(entry.subname)))
		{
			ELOG("Creature query for id " << log_hex_digit(id) << " failed");
			return PacketParseResult::Pass;
		}

		m_creatureCache.NotifyObjectResponse(id, entry);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnItemQueryResult(game::IncomingPacket& packet)
	{
		uint64 id;
		bool succeeded;
		if (!(packet
			>> io::read_packed_guid(id)
			>> io::read<uint8>(succeeded)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!succeeded)
		{
			ELOG("Item query for id " << log_hex_digit(id) << " failed");
			return PacketParseResult::Pass;
		}

		ItemInfo entry{ id };
		if (!(packet >> entry))
		{
			ELOG("Failed to read item info!");
			return PacketParseResult::Disconnect;
		}

		m_itemCache.NotifyObjectResponse(id, entry);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnQuestQueryResult(game::IncomingPacket& packet)
	{
		uint64 id;
		bool succeeded;
		if (!(packet
			>> io::read_packed_guid(id)
			>> io::read<uint8>(succeeded)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!succeeded)
		{
			ELOG("Quest query for id " << log_hex_digit(id) << " failed");
			return PacketParseResult::Pass;
		}

		QuestInfo entry{ id };

		m_questCache.NotifyObjectResponse(id, std::move(entry));
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

		if (const std::shared_ptr<GameUnitC> casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
		{
			if (castTime > 0)
			{
				casterUnit->NotifySpellCastStarted();
			}
		}

		if (m_playerController->GetControlledUnit())
		{
			if (casterId == m_playerController->GetControlledUnit()->GetGuid() && castTime > 0)
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

		if (const std::shared_ptr<GameUnitC> casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
		{
			casterUnit->NotifySpellCastSucceeded();
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

		if (const std::shared_ptr<GameUnitC> casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
		{
			casterUnit->NotifySpellCastCancelled();
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
				m_playerController->GetControlledUnit()->NotifyAttackStopped();

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

		// Find unit
		std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid);
		if (target)
		{
			AddWorldTextFrame(target->GetPosition(), std::to_string(amount), Color(1.0f, 1.0f, 0.0f, 1.0f), 2.0f);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnNonSpellDamageLog(game::IncomingPacket& packet)
	{
		uint64 targetGuid;
		uint32 amount;
		DamageFlags flags;

		if (!(packet
			>> io::read_packed_guid(targetGuid)
			>> io::read<uint32>(amount)
			>> io::read<uint8>(flags)))
		{
			return PacketParseResult::Disconnect;
		}

		std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid);
		if (target)
		{
			AddWorldTextFrame(target->GetPosition(), std::to_string(amount), Color::White, (flags & damage_flags::Crit) != 0 ? 4.0f : 2.0f);
		}

		// TODO: Separate packet for this!
		if (m_playerController && m_playerController->GetControlledUnit())
		{
			m_playerController->GetControlledUnit()->NotifyAttackSwingEvent();
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnLogEnvironmentalDamage(game::IncomingPacket& packet)
	{
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMovementSpeedChanged(game::IncomingPacket& packet)
	{
		static MovementType typesByPacket[] = {
			movement_type::Walk,
			movement_type::Run,
			movement_type::Backwards,
			movement_type::Swim,
			movement_type::SwimBackwards,
			movement_type::Turn,
			movement_type::Flight,
			movement_type::FlightBackwards
		};

		uint64 guid;
		MovementInfo movementInfo;
		float speed;
		if (!(packet >> io::read_packed_guid(guid)
			>> movementInfo >> io::read<float>(speed)))
		{
			return PacketParseResult::Disconnect;
		}

		// Find unit
		std::shared_ptr<GameUnitC> unit = ObjectMgr::Get<GameUnitC>(guid);
		if (!unit)
		{
			return PacketParseResult::Pass;
		}

		// Adjust position
		unit->GetSceneNode()->SetPosition(movementInfo.position);

		MovementType type = movement_type::Run;
		switch(packet.GetId())
		{
		case game::realm_client_packet::MoveSetWalkSpeed:
			type = movement_type::Walk;
			break;
		case game::realm_client_packet::MoveSetRunSpeed:
			type = movement_type::Run;
			break;
		case game::realm_client_packet::MoveSetRunBackSpeed:
			type = movement_type::Backwards;
			break;
		case game::realm_client_packet::MoveSetSwimSpeed:
			type = movement_type::Swim;
			break;
		case game::realm_client_packet::MoveSetSwimBackSpeed:
			type = movement_type::SwimBackwards;
			break;
		case game::realm_client_packet::MoveSetTurnRate:
			type = movement_type::Turn;
			break;
		case game::realm_client_packet::SetFlightSpeed:
			type = movement_type::Flight;
			break;
		case game::realm_client_packet::SetFlightBackSpeed:
			type = movement_type::FlightBackwards;
			break;
		}

		unit->SetSpeed(type, speed);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnForceMovementSpeedChange(game::IncomingPacket& packet)
	{
		const std::shared_ptr<GameUnitC>& unit = m_playerController->GetControlledUnit();
		if (!unit)
		{
			return PacketParseResult::Pass;
		}

		MovementType type = movement_type::Run;
		switch (packet.GetId())
		{
		case game::realm_client_packet::MoveSetWalkSpeed:
			type = movement_type::Walk;
			break;
		case game::realm_client_packet::MoveSetRunSpeed:
			type = movement_type::Run;
			break;
		case game::realm_client_packet::MoveSetRunBackSpeed:
			type = movement_type::Backwards;
			break;
		case game::realm_client_packet::MoveSetSwimSpeed:
			type = movement_type::Swim;
			break;
		case game::realm_client_packet::MoveSetSwimBackSpeed:
			type = movement_type::SwimBackwards;
			break;
		case game::realm_client_packet::MoveSetTurnRate:
			type = movement_type::Turn;
			break;
		case game::realm_client_packet::SetFlightSpeed:
			type = movement_type::Flight;
			break;
		case game::realm_client_packet::SetFlightBackSpeed:
			type = movement_type::FlightBackwards;
			break;
		}

		uint32 ackId;
		float speed;

		if (!(packet >> io::read<uint32>(ackId)
			>> io::read<float>(speed)))
		{
			return PacketParseResult::Disconnect;
		}

		// Apply speed modifier announce locally and send ack to the server
		unit->SetSpeed(type, speed);
		m_realmConnector.SendMovementSpeedAck(type, ackId, speed, unit->GetMovementInfo());

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMoveTeleport(game::IncomingPacket& packet)
	{
		uint64 guid;
		if (!(packet >> io::read_packed_guid(guid)))
		{
			ELOG("Failed to read teleported mover guid!");
			return PacketParseResult::Disconnect;
		}

		const std::shared_ptr<GameUnitC>& unit = m_playerController->GetControlledUnit();
		if (!unit || unit->GetGuid() != guid)
		{
			WLOG("Received teleport for unknown or non-controlled unit!");
			return PacketParseResult::Pass;
		}

		uint32 ackId;
		MovementInfo movementInfo;
		if (!(packet >> io::read<uint32>(ackId) >> movementInfo))
		{
			ELOG("Failed to read move teleport packet");
			return PacketParseResult::Disconnect;
		}

		// Teleport player
		DLOG("Received teleport notification to " << movementInfo.position << ": Applying...");
		unit->ApplyMovementInfo(movementInfo);

		// Send ack to the server
		m_realmConnector.SendMoveTeleportAck(ackId, unit->GetMovementInfo());

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

		std::istringstream strm(tokens[0]);

		uint32 entry = 0;
		strm >> entry;

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

	namespace spell_target_requirements
	{
		enum Type
		{
			None = 0,

			FriendlyUnitTarget = 1 << 0,
			HostileUnitTarget = 1 << 1,
			AnyUnitTarget = FriendlyUnitTarget | HostileUnitTarget,

			AreaTarget = 1 << 2,

			PartyMemberTarget = 1 << 3,
			PetTarget = 1 << 4,
			ObjectTarget = 1 << 5,
		};
	}

	static uint64 GetSpellTargetRequirements(const proto_client::SpellEntry& spell)
	{
		uint64 targetRequirements = spell_target_requirements::None;

		for (const auto& effect : spell.effects())
		{
			switch (effect.targeta())
			{
			case spell_effect_targets::TargetAlly:
				targetRequirements |= spell_target_requirements::FriendlyUnitTarget;
				continue;
			case spell_effect_targets::TargetAny:
				targetRequirements |= spell_target_requirements::AnyUnitTarget;
				continue;
			case spell_effect_targets::TargetEnemy:
				targetRequirements |= spell_target_requirements::HostileUnitTarget;
				continue;
			case spell_effect_targets::ObjectTarget:
				targetRequirements |= spell_target_requirements::ObjectTarget;
				continue;
			case spell_effect_targets::Pet:
				targetRequirements |= spell_target_requirements::PetTarget;
				continue;
			}
		}

		return targetRequirements;
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

		const auto* spell = m_project.spells.getById(entry);
		if (!spell)
		{
			ELOG("Unknown spell");
			return;
		}

		// Check if we need to provide a target unit
		uint64 requirements = GetSpellTargetRequirements(*spell);
		if ((requirements & spell_target_requirements::AnyUnitTarget) != 0)
		{
			// Validate if target unit exists
			std::shared_ptr<GameUnitC> targetUnit = ObjectMgr::Get<GameUnitC>(targetUnitGuid);
			if (!targetUnit)
			{
				// If friendly unit target is required and we have none, target ourself instead automatically
				if ((requirements & spell_target_requirements::FriendlyUnitTarget) != 0 && (requirements & spell_target_requirements::HostileUnitTarget) == 0)
				{
					targetUnit = unit;
				}
				else
				{
					// TODO: Instead of printing an error here we should trigger a selection mode where the user has to click on a target unit instead

					FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FINISH", false);
					FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", "SPELL_CAST_FAILED_BAD_TARGETS");
					ELOG("No target unit selected!");
					return;
				}
			}

			// TODO: There is a target unit, check friend / foe requirements

			// Set target unit
			targetMap.SetTargetMap(spell_cast_target_flags::Unit);
			targetMap.SetUnitTarget(targetUnit->GetGuid());
		}

		if ((spell->interruptflags() & spell_interrupt_flags::Movement) != 0)
		{
			if (unit->GetMovementInfo().IsChangingPosition())
			{
				ELOG("Can't cast spell while moving");
				return;
			}
		}

		if ((spell->attributes(0) & spell_attributes::NotInCombat) != 0 &&
			unit->IsInCombat())
		{
			ELOG("Spell not castable while in combat!");
			return;
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
		m_worldInstance = std::make_unique<ClientWorldInstance>(m_scene, *m_worldRootNode, assetPath);

		const std::unique_ptr<std::istream> streamPtr = AssetRegistry::OpenFile(assetPath + ".hwld");
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
			ELOG("Failed to read world '" << assetPath << ".hwld'!");
			return false;
		}
		
		return true;
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

	void WorldState::AddWorldTextFrame(const Vector3& position, const String& text, const Color& color, float duration)
	{
		auto textFrame = std::make_unique<WorldTextFrame>(m_playerController->GetCamera(), position, duration);
		textFrame->SetText(text);

		// UI styling for rendering
		{
			// TODO: Instead of doing this here hardcoded, lets find a way to make this data driven
			// The reason why this UI element is rendered here manually is because it needs to know of 3d coordinates and convert
			// them into viewspace, which is not possible with the current UI system via xml/lua serialization alone. Also, I don't
			// really want to add exposure of 3d coordinates to the UI script system to reduce the possibility of abuse.
			auto textComponent = std::make_unique<TextComponent>(*textFrame);
			textComponent->SetColor(color);

			ImagerySection section("Text");
			section.AddComponent(std::move(textComponent));

			FrameLayer layer;
			layer.AddSection(*textFrame->AddImagerySection(section));

			StateImagery normalState("Enabled");
			normalState.AddLayer(layer);

			textFrame->AddStateImagery(normalState);
			textFrame->SetRenderer("DefaultRenderer");
		}
		
		m_worldTextFrames.push_back(std::move(textFrame));
	}

	void WorldState::OnPageAvailabilityChanged(const PageNeighborhood& page, bool isAvailable)
	{
		const auto& mainPage = page.GetMainPage();
		const PagePosition& pos = mainPage.GetPosition();

		if (m_worldInstance->HasTerrain())
		{
			if (isAvailable)
			{
				m_worldInstance->GetTerrain()->PreparePage(pos.x(), pos.y());
				m_worldInstance->GetTerrain()->LoadPage(pos.x(), pos.y());
			}
			else
			{
				m_worldInstance->GetTerrain()->UnloadPage(pos.x(), pos.y());
			}
		}
	}

	PagePosition WorldState::GetPagePositionFromCamera() const
	{
		ASSERT(m_playerController);

		const auto& camPos = m_playerController->GetCamera().GetDerivedPosition();
		return PagePosition(static_cast<uint32>(
			32 - floor(camPos.x / terrain::constants::PageSize)),
			32 - static_cast<uint32>(floor(camPos.z / terrain::constants::PageSize)));
	}

	void WorldState::GetPlayerName(uint64 guid, std::weak_ptr<GamePlayerC> player)
	{
		m_playerNameCache.Get(guid, [player](uint64, const String& name)
		{
			if (const std::shared_ptr<GamePlayerC> strong = player.lock())
			{
				strong->SetName(name);
			}
		});
	}

	void WorldState::GetCreatureData(uint64 guid, std::weak_ptr<GameUnitC> creature)
	{
		m_creatureCache.Get(guid, [creature](uint64, const CreatureInfo& data)
		{
			if (const std::shared_ptr<GameUnitC> strong = creature.lock())
			{
				strong->SetCreatureInfo(data);
			}
		});
	}

	void WorldState::GetItemData(uint64 guid, std::weak_ptr<GameItemC> item)
	{
		m_itemCache.Get(guid, [item](uint64, const ItemInfo& data)
			{
				if (const std::shared_ptr<GameItemC> strong = item.lock())
				{
					strong->NotifyItemData(data);
				}
			});
	}

	bool WorldState::GetHeightAt(const Vector3& position, float range, float& out_height)
	{
		float closestHeight = -10000.0f;

		// Do check against terrain?
		if (m_worldInstance->HasTerrain())
		{
			const float terrainHeight = m_worldInstance->GetTerrain()->GetSmoothHeightAt(position.x, position.z);
			if (terrainHeight > closestHeight)
			{
				closestHeight = terrainHeight;
			}
		}

		// TODO: Do raycast against world entity collision geometry instead of just assuming an invisible plane at height 0.0f
		Ray groundDetectionRay(position, position + Vector3::NegativeUnitY * range);
		m_rayQuery->ClearResult();
		m_rayQuery->SetRay(groundDetectionRay);
		m_rayQuery->SetQueryMask(1);
		const RaySceneQueryResult& result = m_rayQuery->Execute();

		if (!result.empty())
		{
			// Reset hit distance to the maximum range we are interested in
			groundDetectionRay.hitDistance = range;
			for(const auto& entry : result)
			{
				Entity* entity = dynamic_cast<Entity*>(entry.movable);
				if (!entity || !entity->GetMesh())
				{
					continue;
				}

				const AABBTree& collisionTree = entity->GetMesh()->GetCollisionTree();
				if (collisionTree.IsEmpty())
				{
					continue;
				}

				// Transform ray into local space of the entity
				const Matrix4 inverse = entity->GetParentNodeFullTransform().Inverse();
				Ray transformedRay(
					inverse * groundDetectionRay.origin,
					inverse * groundDetectionRay.destination);
				transformedRay.hitDistance = range;

				if (collisionTree.IntersectRay(transformedRay, nullptr))
				{
					const Vector3 hitPoint = groundDetectionRay.origin.Lerp(groundDetectionRay.destination, transformedRay.hitDistance);
					ASSERT(hitPoint.y <= position.y);

					if (hitPoint.y > closestHeight)
					{
						closestHeight = hitPoint.y;
					}
				}
			}
		}
		
		// Did we hit something in the range we were interested in?
		if (position.y - closestHeight > range)
		{
			return false;
		}

		out_height = closestHeight;
		return true;
	}

	void WorldState::GetCollisionTrees(const AABB& aabb, std::vector<const Entity*>& out_potentialEntities)
	{
		// TODO: Do check against terrain?

		// TODO: Make more performant check
		for (const Entity* entity : m_scene.GetAllEntities())
		{
			if ((entity->GetQueryFlags() & 1) == 0)
			{
				continue;
			}

			if (!entity->GetMesh() || entity->GetMesh()->GetCollisionTree().IsEmpty())
			{
				continue;
			}

			const AABB& entityAABB = entity->GetWorldBoundingBox(true);
			if (!entityAABB.Intersects(aabb))
			{
				continue;
			}

			out_potentialEntities.push_back(entity);
		}
	}
}
