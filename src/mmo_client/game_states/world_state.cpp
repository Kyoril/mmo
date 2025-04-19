// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

#include <algorithm>
#include <zstr/zstr.hpp>

#include "action_bar.h"
#include "quest_client.h"
#include "game_client/object_mgr.h"
#include "spell_projectile.h"
#include "trainer_client.h"
#include "vendor_client.h"
#include "world_deserializer.h"
#include "base/erase_by_move.h"
#include "base/profiler.h"
#include "base/timer_queue.h"
#include "frame_ui/text_component.h"
#include "game/aura.h"
#include "game/auto_attack.h"
#include "game/chat_type.h"
#include "game/damage_school.h"
#include "game_client/game_player_c.h"
#include "game/spell_target_map.h"
#include "game_client/game_item_c.h"
#include "ui/world_text_frame.h"
#include "game/loot.h"
#include "game/quest.h"
#include "game_client/game_bag_c.h"
#include "terrain/page.h"
#include "guild_client.h"
#include "game/object_info.h"
#include "game/guild_info.h"

#include "audio.h"
#include "party_info.h"
#include "console/console_var.h"
#include "frame_ui/font_mgr.h"
#include "game/group.h"
#include "game_client/game_world_object_c_base.h"
#include "graphics/texture_mgr.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/octree_scene.h"

namespace mmo
{
	const std::string WorldState::Name = "world";

	std::string s_zoneName = "Unknown";
	std::string s_subZoneName = "";

	extern uint32 g_mapId;

	// Console command names
	namespace command_names
	{
		static const char* s_toggleAxis = "ToggleAxis";
		static const char* s_toggleGrid = "ToggleGrid";
		static const char* s_toggleWire = "ToggleWire";
		static const char* s_freezeCulling = "ToggleCullingFreeze";
	}

	namespace
	{
		static ConsoleVar* s_debugOutputPathVar = nullptr;

		static ConsoleVar* s_renderShadowsVar = nullptr;

		static ConsoleVar* s_depthBiasVar = nullptr;
		static ConsoleVar* s_slopeDepthBiasVar = nullptr;
		static ConsoleVar* s_clampDepthBiasVar = nullptr;

		String MapMouseButton(const MouseButton button)
		{
			if ((button & MouseButton::Left) == MouseButton::Left) return "LMB";
			if ((button & MouseButton::Right) == MouseButton::Right) return "RMB";
			if ((button & MouseButton::Middle) == MouseButton::Middle) return "MMB";

			return {};
		}

		String MapBindingKeyCode(const Key keyCode)
		{
			if (keyCode >= 0x70 && keyCode <= 0x87)
			{
				return String("F") + std::to_string(keyCode - 0x70 + 1);
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

			if (keyCode >= 0x60 && keyCode <= 0x69)
			{
				return String("NUM-") + String(1, static_cast<char>(keyCode - 0x60 + '0'));
			}

			switch(keyCode)
			{
			case 0x20:		return "SPACE";
			case 0x0D:		return "ENTER";
			case 0x1B:		return "ESCAPE";
			case 0x08:		return "BACKSPACE";
			case 0x09:		return "TAB";
			case 0x6B:		return "ADD";
			case 0x6D:		return "SUBTRACT";
			case 0x6A:		return "MULTIPLY";
			case 0x6F:		return "DIVIDE";
			case 0x1E:		return "ACCEPT";
			case 0x2E:		return "DEL";
			case 0x2D:		return "INSERT";
			case 0x11:		return "CONTROL";
			case 0x10:		return "SHIFT";
			case 0x25:		return "LEFT";
			case 0x27:		return "RIGHT";
			case 0x26:		return "UP";
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

	WorldState::WorldState(GameStateMgr& gameStateManager, RealmConnector& realmConnector, const proto_client::Project& project, TimerQueue& timers, LootClient& lootClient, VendorClient& vendorClient,
		ActionBar& actionBar, SpellCast& spellCast, TrainerClient& trainerClient, QuestClient& questClient, IAudio& audio, PartyInfo& partyInfo, CharSelect& charSelect, GuildClient& guildClient, ICacheProvider& cache)
		: GameState(gameStateManager)
		, m_realmConnector(realmConnector)
		, m_audio(audio)
		, m_cache(cache)
		, m_project(project)
		, m_timers(timers)
		, m_lootClient(lootClient)
		, m_vendorClient(vendorClient)
		, m_actionBar(actionBar)
		, m_spellCast(spellCast)
		, m_trainerClient(trainerClient)
		, m_questClient(questClient)
		, m_partyInfo(partyInfo)
		, m_charSelect(charSelect)
		, m_guildClient(guildClient)
	{
		// TODO: Do we want to put these asset references in some sort of config setting or something?
		ObjectMgr::SetUnitNameFontSettings(FontManager::Get().CreateOrRetrieve("Fonts/FRIZQT__.TTF", 24.0f, 1.0f), MaterialManager::Get().Load("Models/UnitNameFont.hmat"));
	}

	void WorldState::OnEnter()
	{
		ObjectMgr::Initialize(m_project, m_partyInfo);

		SetupWorldScene();

		// Register world renderer
		FrameManager::Get().RegisterFrameRenderer("WorldRenderer", [this](const std::string& name)
			{
				return std::make_unique<WorldRenderer>(name, *m_scene);
			});

		// Register world frame type
		FrameManager::Get().RegisterFrameFactory("World", [](const std::string& name) {
			auto worldFrame = std::make_shared<WorldFrame>(name);
			worldFrame->SetAsCurrentWorldFrame();
			return worldFrame;
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

		const auto* selectedCharacter = m_charSelect.GetCharacterView(m_charSelect.GetSelectedCharacter());
		ASSERT(selectedCharacter);

		g_mapId = selectedCharacter->GetMapId();

		// Send enter world request to server
		m_realmConnector.EnterWorld(*selectedCharacter);
		m_realmConnector.VerifyNewWorld += [this](uint32 mapId, Vector3 position, float facing)
			{
				if (mapId != g_mapId)
				{
					WLOG("Received different map id from server! Reloading world...");
					g_mapId = mapId;
					LoadMap();
				}
				else
				{
					DLOG("Map " << mapId << " verified");
				}
			};

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

		m_worldRootNode = m_scene->GetRootSceneNode().CreateChildSceneNode();
		LoadMap();

		// Play background music
		m_backgroundMusicSound = m_audio.CreateLoopedStream("Sound/Music/Gethsemane.ogg");
		m_audio.PlaySound(m_backgroundMusicSound, &m_backgroundMusicChannel);

		// Play ambience
		m_ambienceSound = m_audio.CreateSound("Sound/Ambience/ZoneAmbience/ForestNormalDay.wav", SoundType::SoundLooped2D);
		m_audio.PlaySound(m_ambienceSound, &m_ambienceChannel);
	}

	void WorldState::OnLeave()
	{
		m_audio.StopSound(&m_backgroundMusicChannel);
		m_backgroundMusicChannel = InvalidChannel;
		m_backgroundMusicSound = InvalidSound;

		m_audio.StopSound(&m_ambienceChannel);
		m_ambienceChannel = InvalidChannel;
		m_ambienceSound = InvalidSound;

		m_rayQuery.reset();

		// Stop background loading thread
		m_work.reset();
		m_workQueue.stop();
		m_dispatcher.stop();
		m_backgroundLoader.join();

		m_workQueue.reset();
		m_dispatcher.reset();

		m_spellProjectiles.clear();

		ObjectMgr::Initialize(m_project, m_partyInfo);

		m_worldInstance.reset();

		RemovePacketHandler();

		RemoveGameplayCommands();

		s_inputControl = nullptr;
		m_playerController.reset();
		m_worldInstance.reset();
		m_worldGrid.reset();
		m_debugAxis.reset();
		m_scene->Clear();

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

		m_scene.reset();
	}

	std::string_view WorldState::GetName() const
	{
		return Name;
	}

	void WorldState::OnTargetSelectionChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);

		//DLOG("Target changed to " << log_hex_digit(ObjectMgr::GetActivePlayer()->Get<uint64>(object_fields::TargetUnit)));
	}

	void WorldState::OnMoneyChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);
		FrameManager::Get().TriggerLuaEvent("MONEY_CHANGED");
	}

	void WorldState::OnExperiencePointsChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);
		FrameManager::Get().TriggerLuaEvent("PLAYER_XP_CHANGED");
	}

	void WorldState::OnLevelChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);
		FrameManager::Get().TriggerLuaEvent("PLAYER_LEVEL_CHANGED");
		m_questClient.RefreshQuestGiverStatus();
	}

	void WorldState::OnQuestLogChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);
		m_questClient.UpdateQuestLog(*ObjectMgr::GetActivePlayer());
	}

	void WorldState::OnPlayerPowerChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);

		FrameManager::Get().TriggerLuaEvent("PLAYER_POWER_CHANGED");
	}

	void WorldState::OnPlayerHealthChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);

		FrameManager::Get().TriggerLuaEvent("PLAYER_HEALTH_CHANGED");

		if (m_playerController->GetControlledUnit()->GetHealth() <= 0)
		{
			FrameManager::Get().TriggerLuaEvent("PLAYER_DEAD");
		}
	}

	void WorldState::OnPlayerAttributePointsChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);
		FrameManager::Get().TriggerLuaEvent("PLAYER_ATTRIBUTES_CHANGED");
	}

	void WorldState::OnPlayerStatsChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);
		FrameManager::Get().TriggerLuaEvent("PLAYER_ATTRIBUTES_CHANGED");
	}

	void WorldState::OnDisplayIdChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);
		FrameManager::Get().TriggerLuaEvent("PLAYER_MODEL_CHANGED");
	}

	void WorldState::MovementIdleMoveUnits()
	{
		const GameTime timeNow = GetAsyncTimeMs();

		MovementGlobals& globals = ObjectMgr::GetMovementGlobals();
		GameTime timeDiff = timeNow - globals.m_lastUpdateTime;

		// If local mover is set or any movers on the list
		if (true)
		{
			ASSERT(timeDiff > 0);

			// Move units in steps of 1000 ms
			do
			{
				GameTime v14 = globals.m_lastUpdateTime;
				if (timeDiff <= 1000)
				{
					timeDiff = 0;
					Movement::MoveUnits();
				}
				else
				{
					globals.m_lastUpdateTime = v14 + 1000;
					timeDiff -= 1000;
				}

				//if (globals.m_localMover)
				{
					// MoveLocalPlayer();
				}
			} while (timeDiff > 0);
		}
		else
		{
			// We just processed the update right now
			globals.m_lastUpdateTime = timeNow;
		}
	}

	void WorldState::OnCombatModeChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);

		const uint32 flags = ObjectMgr::GetActivePlayer()->Get<uint32>(object_fields::Flags);
		const bool combatMode = (flags & unit_flags::InCombat) != 0;

		if (combatMode != m_combatMode)
		{
			m_combatMode = combatMode;
			FrameManager::Get().TriggerLuaEvent("COMBAT_MODE_CHANGED", m_combatMode);
		}
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
		PROFILE_BEGIN_FRAME();

		m_dispatcher.poll();

		// Update audio component to simulate 3d audio correctly from the player position
		if (m_playerController->GetControlledUnit())
		{
			m_audio.Update(m_playerController->GetControlledUnit()->GetPosition(), deltaSeconds);
		}
		
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

		CheckForZoneUpdate();

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

		PROFILE_END_FRAME();
	}

	void WorldState::CheckForZoneUpdate()
	{
		const auto unit = m_playerController->GetControlledUnit();
		if (!unit)
		{
			return;
		}

		ASSERT(m_worldInstance);
		if (!m_worldInstance->HasTerrain())
		{
			return;
		}

		static uint32 s_zoneId = 0;

		const auto pos = unit->GetPosition();
		const uint32 zoneId = m_worldInstance->GetTerrain()->GetArea(pos);
		if (zoneId != s_zoneId)
		{
			s_zoneId = zoneId;

			const proto_client::ZoneEntry* zone = m_project.zones.getById(s_zoneId);
			if (zone)
			{
				s_zoneName = zone->name();
			}
			else
			{
				s_zoneName = "Unknown";
			}

			FrameManager::Get().TriggerLuaEvent("ZONE_CHANGED");
		}
	}

	void WorldState::SetupWorldScene()
	{
		m_scene = std::make_unique<OctreeScene>();

		m_scene->SetFogRange(210.0f, 300.0f);

		m_cloudsEntity = m_scene->CreateEntity("Clouds", "Models/SkySphere.hmsh");
		m_cloudsEntity->SetRenderQueueGroup(SkiesEarly);
		m_cloudsEntity->SetQueryFlags(0);
		m_cloudsNode = &m_scene->CreateSceneNode("Clouds");
		m_cloudsNode->AttachObject(*m_cloudsEntity);
		m_cloudsNode->SetScale(Vector3::UnitScale * 40.0f);
		m_scene->GetRootSceneNode().AddChild(*m_cloudsNode);

		m_rayQuery = std::move(m_scene->CreateRayQuery(Ray()));
		m_rayQuery->SetSortByDistance(true);
		m_rayQuery->SetQueryMask(1);
		m_rayQuery->SetDebugHitTestResults(true);

		m_playerController = std::make_unique<PlayerController>(*m_scene, m_realmConnector, m_lootClient, m_vendorClient, m_trainerClient);
		s_inputControl = m_playerController.get();

		// Create the world grid in the scene. The world grid component will handle the rest for us
		m_worldGrid = std::make_unique<WorldGrid>(*m_scene, "WorldGrid");
		m_worldGrid->SetVisible(false);

		// Debug axis object
		m_debugAxis = std::make_unique<AxisDisplay>(*m_scene, "WorldDebugAxis");
		m_scene->GetRootSceneNode().AddChild(m_debugAxis->GetSceneNode());
		m_debugAxis->SetVisible(false);

		// Setup sun light
		m_sunLightNode = m_scene->GetRootSceneNode().CreateChildSceneNode("SunLightNode");
		m_sunLight = &m_scene->CreateLight("SunLight", LightType::Directional);
		m_sunLightNode->AttachObject(*m_sunLight);
		m_sunLight->SetDirection({ -0.5f, -1.0f, -0.3f });
		m_sunLight->SetIntensity(1.0f);
		m_sunLight->SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		m_sunLight->SetCastShadows(true);
		m_sunLight->SetShadowFarDistance(75.0f);

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
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveEnded, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveSplineDone, *this, &WorldState::OnMovement);

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
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ObjectQueryResult, *this, &WorldState::OnObjectQueryResult);
		
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

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::LevelUp, *this, &WorldState::OnLevelUp);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::AuraUpdate, *this, &WorldState::OnAuraUpdate);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::PeriodicAuraLog, *this, &WorldState::OnPeriodicAuraLog);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ActionButtons, *this, &WorldState::OnActionButtons);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::QuestGiverStatus, *this, &WorldState::OnQuestGiverStatus);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SpellEnergizeLog, *this, &WorldState::OnSpellEnergizeLog);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::TransferPending, *this, &WorldState::OnTransferPending);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ItemPushResult, *this, &WorldState::OnItemPushResult);
		
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::PartyCommandResult, *this, &WorldState::OnPartyCommandResult);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::GroupInvite, *this, &WorldState::OnGroupInvite);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::GroupDecline, *this, &WorldState::OnGroupDecline);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::RandomRollResult, *this, &WorldState::OnRandomRollResult);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::AttackerStateUpdate, *this, &WorldState::OnAttackerStateUpdate);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SpellHealLog, *this, &WorldState::OnSpellHealLog);
		
		m_lootClient.Initialize();
		m_vendorClient.Initialize();
		m_trainerClient.Initialize();
		m_questClient.Initialize();
		m_partyInfo.Initialize();
		m_guildClient.Initialize();

#ifdef MMO_WITH_DEV_COMMANDS
		Console::RegisterCommand("createmonster", [this](const std::string& cmd, const std::string& args) { Command_CreateMonster(cmd, args); }, ConsoleCommandCategory::Gm, "Spawns a monster from a specific id. The monster will not persist on server restart.");
		Console::RegisterCommand("destroymonster", [this](const std::string& cmd, const std::string& args) { Command_DestroyMonster(cmd, args); }, ConsoleCommandCategory::Gm, "Destroys a spawned monster from a specific guid.");
		Console::RegisterCommand("learnspell", [this](const std::string& cmd, const std::string& args) { Command_LearnSpell(cmd, args); }, ConsoleCommandCategory::Gm, "Makes the selected player learn a given spell.");
		Console::RegisterCommand("followme", [this](const std::string& cmd, const std::string& args) { Command_FollowMe(cmd, args); }, ConsoleCommandCategory::Gm, "Makes the selected creature follow you.");
		Console::RegisterCommand("faceme", [this](const std::string& cmd, const std::string& args) { Command_FaceMe(cmd, args); }, ConsoleCommandCategory::Gm, "Makes the selected creature face towards you.");
		Console::RegisterCommand("level", [this](const std::string& cmd, const std::string& args) { Command_LevelUp(cmd, args); }, ConsoleCommandCategory::Gm, "Increases the targets level if possible.");
		Console::RegisterCommand("money", [this](const std::string& cmd, const std::string& args) { Command_GiveMoney(cmd, args); }, ConsoleCommandCategory::Gm, "Increases the targets money.");
		Console::RegisterCommand("additem", [this](const std::string& cmd, const std::string& args) { Command_AddItem(cmd, args); }, ConsoleCommandCategory::Gm, "Adds an item to the target players inventory.");
		Console::RegisterCommand("worldport", [this](const std::string& cmd, const std::string& args) { Command_WorldPort(cmd, args); }, ConsoleCommandCategory::Gm, "Teleports the player to the given map, optionally also changing the position.");
		Console::RegisterCommand("port", [this](const std::string& cmd, const std::string& args) { Command_Port(cmd, args); }, ConsoleCommandCategory::Gm, "Teleports you to the named player if he exists.");
		Console::RegisterCommand("summon", [this](const std::string& cmd, const std::string& args) { Command_Summon(cmd, args); }, ConsoleCommandCategory::Gm, "Summons the named player to your location if such a player exists.");
		Console::RegisterCommand("speed", [this](const std::string& cmd, const std::string& args) { Command_Speed(cmd, args); }, ConsoleCommandCategory::Gm, "Sets your movement speed to the new value in meters per second.");
#endif
	}

	void WorldState::RemovePacketHandler()
	{
#ifdef MMO_WITH_DEV_COMMANDS
		Console::UnregisterCommand("createmonster");
		Console::UnregisterCommand("destroymonster");
		Console::UnregisterCommand("learnspell");
		Console::UnregisterCommand("followme");
		Console::UnregisterCommand("faceme");
		Console::UnregisterCommand("level");
		Console::UnregisterCommand("money");
		Console::UnregisterCommand("additem");
		Console::UnregisterCommand("worldport");
		Console::UnregisterCommand("port");
		Console::UnregisterCommand("summon");
		Console::UnregisterCommand("speed");
#endif

		m_guildClient.Shutdown();
		m_partyInfo.Shutdown();
		m_questClient.Shutdown();
		m_trainerClient.Shutdown();
		m_lootClient.Shutdown();
		m_vendorClient.Shutdown();

		m_worldPacketHandlers.Clear();
		m_worldChangeHandlers.Clear();
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
		s_debugOutputPathVar = ConsoleVarMgr::RegisterConsoleVar("DebugTargetPath", "", "0");
		s_renderShadowsVar = ConsoleVarMgr::RegisterConsoleVar("RenderShadows", "Determines whether or not shadows should be rendered", "1");
		m_cvarChangedSignals += s_renderShadowsVar->Changed.connect(this, &WorldState::OnRenderShadowsChanged);

		s_depthBiasVar = ConsoleVarMgr::RegisterConsoleVar("ShadowDepthBias", "", "250");
		m_cvarChangedSignals += s_depthBiasVar->Changed.connect(this, &WorldState::OnShadowBiasChanged);
		s_slopeDepthBiasVar = ConsoleVarMgr::RegisterConsoleVar("ShadowSlopeBias", "", "1.0");
		m_cvarChangedSignals += s_slopeDepthBiasVar->Changed.connect(this, &WorldState::OnShadowBiasChanged);
		s_clampDepthBiasVar = ConsoleVarMgr::RegisterConsoleVar("ShadowClampBias", "", "0");
		m_cvarChangedSignals += s_clampDepthBiasVar->Changed.connect(this, &WorldState::OnShadowBiasChanged);

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

		Console::RegisterCommand(command_names::s_freezeCulling, [this](const std::string&, const std::string&)
			{
				// Ensure that the frustum planes are recalculated to immediately see the effect
				m_playerController->GetCamera().InvalidateView();

				// Toggle culling freeze and log current culling state
				m_scene->FreezeRendering(!m_scene->IsRenderingFrozen());
				ILOG(m_scene->IsRenderingFrozen() ? "Culling is now frozen" : "Culling is no longer frozen");
			}, ConsoleCommandCategory::Debug, "Toggles culling.");

	}

	void WorldState::RemoveGameplayCommands()
	{
		ConsoleVarMgr::UnregisterConsoleVar("DebugTargetPath");
		ConsoleVarMgr::UnregisterConsoleVar("RenderShadows");
		ConsoleVarMgr::UnregisterConsoleVar("ShadowDepthBias");
		ConsoleVarMgr::UnregisterConsoleVar("ShadowSlopeBias");
		ConsoleVarMgr::UnregisterConsoleVar("ShadowClampBias");

		m_cvarChangedSignals.disconnect();

		const String commandsToRemove[] = {
			command_names::s_toggleAxis,
			command_names::s_toggleGrid,
			command_names::s_toggleWire,
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
					object = std::make_shared<GameUnitC>(*m_scene, *this, *this, m_project, g_mapId);
					break;
				case ObjectTypeId::Player:
					object = std::make_shared<GamePlayerC>(*m_scene, *this, *this, m_project, g_mapId);
					break;
				case ObjectTypeId::Item:
					object = std::make_shared<GameItemC>(*m_scene, *this, m_project);
					break;
				case ObjectTypeId::Container:
					object = std::make_shared<GameBagC>(*m_scene, *this, m_project);
					break;
				case ObjectTypeId::Object:
					object = std::make_shared<GameWorldObjectC_Chest>(*m_scene, m_project, *this, g_mapId);
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

				// Ensure we update the quest status of quest givers
				if (object->GetTypeId() == ObjectTypeId::Unit)
				{
					if (object->Get<uint32>(object_fields::NpcFlags) & npc_flags::QuestGiver)
					{
						m_realmConnector.UpdateQuestStatus(object->GetGuid());
					}
				}

				if (object->GetTypeId() == ObjectTypeId::Player)
				{
					m_partyInfo.OnPlayerSpawned(static_cast<GamePlayerC&>(*object));
				}

				// TODO: Don't do it like this, add a special flag to the update object to tell that this is our controlled object!
				if (!m_playerController->GetControlledUnit() && object->GetTypeId() == ObjectTypeId::Player)
				{
					ObjectMgr::SetActivePlayer(object->GetGuid());

					// Register player field change observers
					m_playerObservers += object->RegisterMirrorHandler(object_fields::TargetUnit, 2, *this, &WorldState::OnTargetSelectionChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::Money, 1, *this, &WorldState::OnMoneyChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::Xp, 2, *this, &WorldState::OnExperiencePointsChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::Level, 1, *this, &WorldState::OnLevelChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::QuestLogSlot_1, (sizeof(QuestField) / sizeof(uint32)) * MaxQuestLogSize, *this, &WorldState::OnQuestLogChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::Mana, 7, *this, &WorldState::OnPlayerPowerChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::Health, 2, *this, &WorldState::OnPlayerHealthChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::AvailableAttributePoints, 1, *this, &WorldState::OnPlayerAttributePointsChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::DisplayId, 1, *this, &WorldState::OnDisplayIdChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::Flags, 1, *this, &WorldState::OnCombatModeChanged);

					m_playerObservers += object->RegisterMirrorHandler(object_fields::AttackPower, 1, *this, &WorldState::OnPlayerStatsChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::Armor, 1, *this, &WorldState::OnPlayerStatsChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::StatStamina, 15, *this, &WorldState::OnPlayerStatsChanged);
					m_playerObservers += object->RegisterMirrorHandler(object_fields::BaseAttackTime, 3, *this, &WorldState::OnPlayerStatsChanged);
					static_assert(object_fields::MinDamage == object_fields::BaseAttackTime + 1, "Order of fields is important for monitored unit mirror handlers");
					static_assert(object_fields::MaxDamage == object_fields::MinDamage + 1, "Order of fields is important for monitored unit mirror handlers");

					// Old handlers for now
					m_playerObservers += object->fieldsChanged.connect([this](uint64 guid, uint16 fieldIndex, uint16 fieldCount)
						{
							if ((object_fields::PackSlot_1 > fieldIndex && object_fields::InvSlotHead <= fieldIndex + fieldCount))
							{
								FrameManager::Get().TriggerLuaEvent("EQUIPMENT_CHANGED");
							}

							if ((object_fields::BankSlot_1 > fieldIndex && object_fields::InvSlotHead <= fieldIndex + fieldCount))
							{
								FrameManager::Get().TriggerLuaEvent("INVENTORY_CHANGED");
							}
						});

					m_playerController->SetControlledUnit(std::dynamic_pointer_cast<GameUnitC>(object));
					FrameManager::Get().TriggerLuaEvent("PLAYER_ENTER_WORLD");

					if (m_playerController->GetControlledUnit()->GetHealth() <= 0)
					{
						FrameManager::Get().TriggerLuaEvent("PLAYER_DEAD");
					}

					m_questClient.UpdateQuestLog(*std::static_pointer_cast<GamePlayerC>(object));

					OnCombatModeChanged(object->GetGuid());
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
					if (obj->GetGuid() != ObjectMgr::GetActivePlayerGuid())
					{
						if (const auto unit = std::dynamic_pointer_cast<GameUnitC>(obj))
						{
							const uint64 targetGuid = unit->Get<uint64>(object_fields::TargetUnit);
							unit->SetTargetUnit(targetGuid ? ObjectMgr::Get<GameUnitC>(targetGuid) : nullptr);
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

			if (id == ObjectMgr::GetSelectedObjectGuid())
			{
				ObjectMgr::GetActivePlayer()->SetTargetUnit(nullptr);
			}

			m_partyInfo.OnPlayerDespawned(id);

			if (m_playerController->GetControlledUnit() &&
				m_playerController->GetControlledUnit()->GetGuid() == id)
			{
				ELOG("Despawn of player controlled object!");
				m_playerController->SetControlledUnit(nullptr);
			}
			
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

		if (type == ChatType::UnitSay || type == ChatType::UnitEmote || type == ChatType::UnitYell)
		{
			String speaker;
			if (!(packet >> io::read_container<uint8>(speaker)))
			{
				return PacketParseResult::Disconnect;
			}

			String chatMessageType;
			switch (type)
			{
			case ChatType::UnitSay: chatMessageType = "UNIT_SAY"; break;
			case ChatType::UnitYell: chatMessageType = "UNIT_YELL"; break;
			case ChatType::UnitEmote: chatMessageType = "UNIT_EMOTE"; break;
			default:
				UNREACHABLE();
				break;
			}

			FrameManager::Get().TriggerLuaEvent("CHAT_MSG_" + chatMessageType, speaker, message);
		}
		else
		{
			m_cache.GetNameCache().Get(characterGuid, [this, type, message, flags](uint64, const String& name)
				{
					String chatMessageType = "SAY";
					switch (type)
					{
					case ChatType::Channel: chatMessageType = "CHANNEL"; break;
					case ChatType::Yell: chatMessageType = "YELL"; break;
					case ChatType::Group: chatMessageType = "PARTY"; break;
					case ChatType::Guild: chatMessageType = "GUILD"; break;
					case ChatType::Whisper: chatMessageType = "WHISPER"; break;
					case ChatType::Raid: chatMessageType = "RAID"; break;
					}

					FrameManager::Get().TriggerLuaEvent("CHAT_MSG_" + chatMessageType, name, message);
				});
		}
		
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnNameQueryResult(game::IncomingPacket& packet)
	{
		uint64 guid;
		bool succeeded;
		String name;
		if (!(packet
			>> io::read_packed_guid(guid)
			>> io::read<uint8>(succeeded)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!succeeded)
		{
			ELOG("Unable to retrieve unit name for unit " << log_hex_digit(guid));
			return PacketParseResult::Pass;
		}

		if (!(packet
			>> io::read_string(name)))
		{
			return PacketParseResult::Disconnect;
		}

		m_cache.GetNameCache().NotifyObjectResponse(guid, name);

		if (m_playerController->GetControlledUnit())
		{
			if (guid == m_playerController->GetControlledUnit()->GetGuid())
			{
				FrameManager::Get().TriggerLuaEvent("UNIT_NAME_UPDATE", "player");
			}
			if (guid == m_playerController->GetControlledUnit()->Get<uint64>(object_fields::TargetUnit))
			{
				FrameManager::Get().TriggerLuaEvent("UNIT_NAME_UPDATE", "target");
			}

			for (uint32 i = 0; i < m_partyInfo.GetMemberCount(); ++i)
			{
				if (guid == m_partyInfo.GetMemberGuid(i))
				{
					FrameManager::Get().TriggerLuaEvent("UNIT_NAME_UPDATE", "party" + std::to_string(i + 1));
				}
			}
		}

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

		m_cache.GetCreatureCache().NotifyObjectResponse(id, entry);
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

		m_cache.GetItemCache().NotifyObjectResponse(id, entry);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnObjectQueryResult(game::IncomingPacket& packet)
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
			ELOG("Object query for id " << log_hex_digit(id) << " failed");
			return PacketParseResult::Pass;
		}

		ObjectInfo entry{ id };
		if (!(packet >> entry))
		{
			ELOG("Failed to read object info!");
			return PacketParseResult::Disconnect;
		}

		m_cache.GetObjectCache().NotifyObjectResponse(id, entry);
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
		GameTime startTime;
		GameTime endTime;
		uint32 pathSize;
		std::optional<Radian> targetRotation;

		if (!(packet 
			>> io::read_packed_guid(guid)
			>> io::read<float>(startPosition.x)
			>> io::read<float>(startPosition.y)
			>> io::read<float>(startPosition.z)
			>> io::read<GameTime>(startTime)
			>> io::read<GameTime>(endTime)
			>> io::read<uint32>(pathSize)
			>> io::read<float>(endPosition.x)
			>> io::read<float>(endPosition.y)
			>> io::read<float>(endPosition.z)))
		{
			return PacketParseResult::Disconnect;
		}

		uint8 hasTargetRotation;
		if (!(packet >> io::read<uint8>(hasTargetRotation)))
		{
			return PacketParseResult::Disconnect;
		}

		if (hasTargetRotation)
		{
			float rotation;
			if (!(packet >> io::read<float>(rotation)))
			{
				return PacketParseResult::Disconnect;
			}

			targetRotation = Radian(rotation);
		}

		// Find unit by guid
		const auto unitPtr = ObjectMgr::Get<GameUnitC>(guid);
		if(!unitPtr)
		{
			// We don't know the unit
			WLOG("Received movement packet for unknown unit id " << log_hex_digit(guid));
			return PacketParseResult::Pass;
		}

		const bool isPlayer = unitPtr->GetTypeId() == ObjectTypeId::Player;
		(void)isPlayer;

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

		// Check whether we should update the output debug path
		if (guid == ObjectMgr::GetSelectedObjectGuid())
		{
			if (s_debugOutputPathVar && s_debugOutputPathVar->GetBoolValue())
			{
				// Update it!
				DLOG("Creature path received (Size: " << path.size() << ")");

				size_t index = 0;
				for (auto& p : path)
				{
					DLOG("\t#" << index++ << ": " << p);
				}
			}
			else
			{
				// Ensure it is not visible

			}
		}
		
		unitPtr->SetMovementPath(path, endTime - startTime, targetRotation);

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

			// Add this spell to the players action bar if there is any unassigned button in the main bar, but only if it is not a passive spell and its an ability
			if ((spell->attributes(0) & spell_attributes::Passive) == 0 &&
				(spell->attributes(0) & spell_attributes::Ability) != 0)
			{
				int32 emptySlot = -1;

				for (int32 i = 0; i < MaxActionButtons; ++i)
				{
					// Did we find an empty slot yet?
					if (emptySlot == -1 && m_actionBar.GetActionButton(i).type == action_button_type::None)
					{
						// This is the first empty slot
						emptySlot = i;
					}

					const auto* spellAtSlot = m_actionBar.GetActionButtonSpell(i);
					if (spellAtSlot != nullptr && spellAtSlot->id() == spellId)
					{
						// The spell is already on the action bar, stop processing and erase potential empty slot found
						emptySlot = -1;
						break;
					}
				}

				// If we should set this button to the action bar, do it
				if (emptySlot != -1)
				{
					m_actionBar.SetActionButton(emptySlot, { static_cast<uint16>(spellId), action_button_type::Spell });
				}
			}

			// Update possible trainer spell list
			m_trainerClient.OnSpellLearned(spellId);

			FrameManager::Get().TriggerLuaEvent("SPELL_LEARNED", spell->name());
		}
		else
		{
			m_playerController->GetControlledUnit()->UnlearnSpell(spellId);
		}

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
				m_spellCast.OnSpellStart(*spell, castTime);
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
					auto projectile = std::make_unique<SpellProjectile>(*m_scene, *spell, casterUnit->GetSceneNode()->GetDerivedPosition(), targetUnit);
					m_spellProjectiles.push_back(std::move(projectile));
				}
			}
		}

		if (const std::shared_ptr<GameUnitC> casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
		{
			casterUnit->NotifySpellCastSucceeded();
		}

		if (casterId == ObjectMgr::GetActivePlayerGuid())
		{
			m_spellCast.OnSpellGo(spellId);
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

		if (casterId == ObjectMgr::GetActivePlayerGuid())
		{
			m_spellCast.OnSpellFailure(spellId);

			const char* errorMessage = s_unknown;
			if (result < std::size(s_spellCastResultStrings))
			{
				errorMessage = s_spellCastResultStrings[result];
			}

			FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FINISH", false);
			FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", errorMessage);
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
		uint32 xp;
		if (!(packet >> io::read<uint32>(xp)))
		{
			return PacketParseResult::Disconnect;
		}

		// Find unit
		std::shared_ptr<GamePlayerC> target = ObjectMgr::GetActivePlayer();
		if (target)
		{
			AddWorldTextFrame(target->GetPosition(), std::to_string(xp) + " XP", Color(1.0f, 0.0f, 0.85f, 1.0f), 2.0f);
		}

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

	PacketParseResult WorldState::OnAttackerStateUpdate(game::IncomingPacket& packet)
	{
		uint64 attackerGuid, attackedGuid;
		uint32 totalDamage, school, absorbedDamage, resistedDamage, blockedDamage;
		VictimState victimState;
		HitInfo hitInfo;
		if (!(packet
			>> io::read_packed_guid(attackerGuid)
			>> io::read_packed_guid(attackedGuid)
			>> io::read<uint32>(hitInfo)
			>> io::read<uint32>(victimState)
			>> io::read<uint32>(totalDamage)
			>> io::read<uint32>(school)
			>> io::read<uint32>(absorbedDamage)
			>> io::read<uint32>(resistedDamage)
			>> io::read<uint32>(blockedDamage)))
		{
			return PacketParseResult::Disconnect;
		}

		std::shared_ptr<GameUnitC> attacker = ObjectMgr::Get<GameUnitC>(attackerGuid);
		if (attacker)
		{
			attacker->NotifyAttackSwingEvent();
		}

		std::shared_ptr<GameUnitC> attacked = ObjectMgr::Get<GameUnitC>(attackedGuid);
		if (attacked)
		{
			attacked->NotifyHitEvent();

			if (ObjectMgr::GetActivePlayerGuid() == attackerGuid || ObjectMgr::GetActivePlayerGuid() == attackedGuid)
			{
				String damageText;
				if (hitInfo & hit_info::Miss)
				{
					damageText = "MISSED";	// Localize
				}
				else if (victimState == victim_state::Parry)
				{
					damageText = "PARRIED";
				}
				else if (victimState == victim_state::Dodge)
				{
					damageText = "DODGED";
				}
				else if (victimState == victim_state::Blocks)
				{
					damageText = "BLOCKED";
				}
				else
				{
					damageText = std::to_string(totalDamage);
				}

				AddWorldTextFrame(attacked->GetPosition(), damageText, ObjectMgr::GetActivePlayerGuid() == attackedGuid ? Color(1.0f, 0.0f, 0.0f) : Color::White, (hitInfo & hit_info::CriticalHit) != 0 ? 4.0f : 2.0f);
			}
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
			WLOG("No controlled unit found!");
			return PacketParseResult::Pass;
		}

		MovementType type = movement_type::Run;
		switch (packet.GetId())
		{
		case game::realm_client_packet::ForceMoveSetWalkSpeed:
			type = movement_type::Walk;
			break;
		case game::realm_client_packet::ForceMoveSetRunSpeed:
			type = movement_type::Run;
			break;
		case game::realm_client_packet::ForceMoveSetRunBackSpeed:
			type = movement_type::Backwards;
			break;
		case game::realm_client_packet::ForceMoveSetSwimSpeed:
			type = movement_type::Swim;
			break;
		case game::realm_client_packet::ForceMoveSetSwimBackSpeed:
			type = movement_type::SwimBackwards;
			break;
		case game::realm_client_packet::ForceMoveSetTurnRate:
			type = movement_type::Turn;
			break;
		case game::realm_client_packet::ForceSetFlightSpeed:
			type = movement_type::Flight;
			break;
		case game::realm_client_packet::ForceSetFlightBackSpeed:
			type = movement_type::FlightBackwards;
			break;
		}

		uint32 ackId;
		float speed;

		if (!(packet >> io::read<uint32>(ackId)
			>> io::read<float>(speed)))
		{
			WLOG("Failed to read force movement speed change packet!");
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

		m_playerController->StopAllMovement();

		// Teleport player
		DLOG("Received teleport notification to " << movementInfo.position << ": Applying...");
		unit->ApplyMovementInfo(movementInfo);

		// Send ack to the server
		m_realmConnector.SendMoveTeleportAck(ackId, unit->GetMovementInfo());

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnLevelUp(game::IncomingPacket& packet)
	{
		uint32 newLevel;
		int32 healthDiff, manaDiff, staminaDiff, strengthDiff, agilityDiff, intDiff, spiritDiff, talentPoints, attributePoints;
		if (!(packet
			>> io::read<uint8>(newLevel)
			>> io::read<int32>(healthDiff)
			>> io::read<int32>(manaDiff)
			>> io::read<int32>(staminaDiff)
			>> io::read<int32>(strengthDiff)
			>> io::read<int32>(agilityDiff)
			>> io::read<int32>(intDiff)
			>> io::read<int32>(spiritDiff)
			>> io::read<int32>(talentPoints)
			>> io::read<int32>(attributePoints)))
		{
			ELOG("Failed to read levelup packet!");
			return PacketParseResult::Disconnect;
		}

		FrameManager::Get().TriggerLuaEvent("PLAYER_LEVEL_UP", newLevel, healthDiff, manaDiff, staminaDiff, strengthDiff, agilityDiff, intDiff, spiritDiff, talentPoints, attributePoints);

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnAuraUpdate(game::IncomingPacket& packet)
	{
		uint64 unitGuid;
		if (!(packet >> io::read_packed_guid(unitGuid)))
		{
			ELOG("Failed to read AuraUpdate packet!");
			return PacketParseResult::Disconnect;
		}

		// Try to find unit
		std::shared_ptr<GameUnitC> unit = ObjectMgr::Get<GameUnitC>(unitGuid);
		if (!unit)
		{
			WLOG("Unable to find unit " << log_hex_digit(unitGuid) << " for aura update!");
			return PacketParseResult::Pass;
		}

		if (!unit->OnAuraUpdate(packet))
		{
			return PacketParseResult::Disconnect;
		}

		if (unit->GetGuid() == ObjectMgr::GetActivePlayerGuid())
		{
			FrameManager::Get().TriggerLuaEvent("PLAYER_AURA_UPDATE");
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnPeriodicAuraLog(game::IncomingPacket& packet)
	{
		uint64 targetGuid;
		uint64 casterGuid;
		uint32 spellId;
		uint32 auraType;

		if (!(packet >> io::read_packed_guid(targetGuid)
			>> io::read_packed_guid(casterGuid)
			>> io::read<uint32>(spellId)
			>> io::read<uint32>(auraType)))
		{
			ELOG("Failed to read PeriodicAuraLog packet!");
			return PacketParseResult::Disconnect;
		}

		if (auraType == aura_type::PeriodicDamage ||
			auraType == aura_type::PeriodicHeal)
		{
			Color color;
			if (auraType == aura_type::PeriodicHeal)
			{
				color = Color(0.0f, 1.0f, 0.0f, 1.0f);
			}
			else
			{
				color = Color(1.0f, 1.0f, 0.0f, 1.0f);
			}

			uint32 amount;
			if (!(packet >> io::read<uint32>(amount)))
			{
				ELOG("Failed to read amount from PeriodicAuraLog packet!");
				return PacketParseResult::Disconnect;
			}

			if (casterGuid == ObjectMgr::GetActivePlayerGuid() || targetGuid == ObjectMgr::GetActivePlayerGuid())
			{
				if (const std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid))
				{
					AddWorldTextFrame(target->GetPosition(), std::to_string(amount), color, 2.0f);
				}
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnActionButtons(game::IncomingPacket& packet)
	{
		// Read packet
		m_actionBar.OnActionButtons(packet);

		if (!packet)
		{
			ELOG("Failed to read ActionButtons packet!");
			return PacketParseResult::Disconnect;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnQuestGiverStatus(game::IncomingPacket& packet)
	{
		// Read packet
		uint64 questgiverGuid;
		QuestgiverStatus status;
		if (!(packet >> io::read<uint64>(questgiverGuid) >> io::read<uint8>(status)))
		{
			ELOG("Failed to read QuestGiverStatus packet!");
			return PacketParseResult::Disconnect;
		}

		// Check on status
		ASSERT(questgiverGuid != 0);
		ASSERT(status < questgiver_status::Count_);

		// Find unit
		std::shared_ptr<GameUnitC> questgiverUnit = ObjectMgr::Get<GameUnitC>(questgiverGuid);
		ASSERT(questgiverUnit);

		questgiverUnit->SetQuestgiverStatus(status);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellEnergizeLog(game::IncomingPacket& packet)
	{
		uint64 targetGuid, casterGuid;
		uint32 spellId, powerType, power;
		if (!(packet >> io::read_packed_guid(targetGuid)
			>> io::read_packed_guid(casterGuid)
			>> io::read<uint32>(spellId)
			>> io::read<uint32>(powerType)
			>> io::read<uint32>(power)))
		{
			ELOG("Failed to read SpellEnergizeLog packet!");
			return PacketParseResult::Disconnect;
		}

		// TODO: Combat log event

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnTransferPending(game::IncomingPacket& packet)
	{
		uint32 mapId;
		if (!(packet >> io::read<uint32>(mapId)))
		{
			ELOG("Failed to read TransferPending packet!");
			return PacketParseResult::Disconnect;
		}

		ILOG("Transfer pending to map " << mapId << "...");
		if (g_mapId == mapId)
		{
			return PacketParseResult::Pass;
		}

		g_mapId = mapId;

		m_worldChangeHandlers.Clear();
		m_worldChangeHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::NewWorld, *this, &WorldState::OnNewWorld);

		// Remove all objects at once
		m_playerController->SetControlledUnit(nullptr);
		ObjectMgr::RemoveAllObjects();

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnNewWorld(game::IncomingPacket& packet)
	{
		uint32 mapId;
		Vector3 position;
		float facingRadianVal;
		if (!(packet
			>> io::read<uint32>(mapId)
			>> io::read<float>(position.x)
			>> io::read<float>(position.y)
			>> io::read<float>(position.z)
			>> io::read<float>(facingRadianVal)))
		{
			ELOG("Failed to read NewWorld packet!");
			return PacketParseResult::Disconnect;
		}

		ILOG("Transfer to map " << mapId << " completed! Loading map...");
		g_mapId = mapId;

		// Load new map
		LoadMap();

		// Ensure terrain is properly reloaded
		PagePosition worldSize(64, 64);
		const auto pagePos = PagePosition(static_cast<uint32>(floor(position.x / terrain::constants::PageSize)) + 32, static_cast<uint32>(floor(position.z / terrain::constants::PageSize)) + 32);
		ForEachPageInSquare(
			worldSize, pagePos, 1, [this](const PagePosition& page)
			{
				terrain::Page* terrainPage = m_worldInstance->GetTerrain()->GetPage(page.x(), page.y());
				if (terrainPage)
				{
					if (terrainPage->Prepare())
					{
						while (!terrainPage->Load());
					}
				}
			}
		);

		// Acknowledge the new world
		m_realmConnector.SendMoveWorldPortAck();

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnGroupInvite(game::IncomingPacket& packet)
	{
		String inviterName;
		if (!(packet >> io::read_container<uint8>(inviterName)))
		{
			ELOG("Failed to read GroupInvite packet!");
			return PacketParseResult::Disconnect;
		}

		// Notify group invitation
		FrameManager::Get().TriggerLuaEvent("PARTY_INVITE_REQUEST", inviterName);

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnGroupDecline(game::IncomingPacket& packet)
	{
		String inviterName;
		if (!(packet >> io::read_container<uint8>(inviterName)))
		{
			ELOG("Failed to read GroupInvite packet!");
			return PacketParseResult::Disconnect;
		}

		FrameManager::Get().TriggerLuaEvent("PARTY_INVITE_DECLINED", inviterName);

		// TODO
		ELOG(inviterName << " declines your group invitation.");
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnPartyCommandResult(game::IncomingPacket& packet)
	{
		PartyOperation command;
		PartyResult result;
		String playerName;
		if (!(packet
			>> io::read<uint8>(command)
			>> io::read_container<uint8>(playerName)
			>> io::read<uint8>(result)))
		{
			ELOG("Failed to read PartyCommandResult packet!");
			return PacketParseResult::Disconnect;
		}

		// Was it an error?
		if (result != party_result::Ok)
		{
			FrameManager::Get().TriggerLuaEvent("PARTY_COMMAND_RESULT", static_cast<int32>(result), playerName);
		}
		else
		{
			if (command == party_operation::Invite)
			{
				FrameManager::Get().TriggerLuaEvent("PARTY_INVITE_SENT", playerName);
			}
			else if (command == party_operation::Leave)
			{
				FrameManager::Get().TriggerLuaEvent("PARTY_LEFT");
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnRandomRollResult(game::IncomingPacket& packet)
	{
		uint64 playerGuid;
		int32 min, max, result;
		if (!(packet >> io::read<uint64>(playerGuid) >> io::read<int32>(min) >> io::read<int32>(max) >> io::read<int32>(result)))
		{
			ELOG("Failed to read RandomRollResult packet!");
			return PacketParseResult::Disconnect;
		}

		m_cache.GetNameCache().Get(playerGuid, [result, min, max](uint64 guid, const String& playerName)
			{
				FrameManager::Get().TriggerLuaEvent("RANDOM_ROLL_RESULT", playerName.c_str(), min, max, result);
			});

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellHealLog(game::IncomingPacket& packet)
	{
		uint64 targetGuid, casterGuid;
		uint32 amount;
		bool crit;
		uint32 spellId;

		if (!(packet
			>> io::read_packed_guid(targetGuid)
			>> io::read_packed_guid(casterGuid)
			>> io::read<uint32>(spellId)
			>> io::read<uint32>(amount)
			>> io::read<uint8>(crit)))
		{
			return PacketParseResult::Disconnect;
		}

		// If the target or the caster is our player, we want to display the heal amount
		if (casterGuid == ObjectMgr::GetActivePlayerGuid() || targetGuid == ObjectMgr::GetActivePlayerGuid())
		{
			// Find unit
			std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid);
			if (target)
			{
				AddWorldTextFrame(target->GetPosition(), std::to_string(amount), Color(0.0f, 1.0f, 0.0f, 1.0f), 2.0f);
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnItemPushResult(game::IncomingPacket& packet)
	{
		uint64 characterGuid;
		bool wasLooted, wasCreated;
		uint8 bag, subslot;
		uint32 itemId;
		uint16 amount, totalCount;

		if (!(packet
			>> io::read<uint64>(characterGuid)
			>> io::read<uint8>(wasLooted)
			>> io::read<uint8>(wasCreated)
			>> io::read<uint8>(bag)
			>> io::read<uint8>(subslot)
			>> io::read<uint32>(itemId)
			>> io::read<uint16>(amount)
			>> io::read<uint16>(totalCount)))
		{
			ELOG("Failed to read ItemPushResult packet!");
			return PacketParseResult::Disconnect;
		}

		// Ask for the item info
		m_cache.GetItemCache().Get(itemId, [this, characterGuid, wasLooted, wasCreated, bag, subslot, amount, totalCount](uint64, const ItemInfo& itemInfo)
		{
			OnItemPushCallback(itemInfo, characterGuid, wasLooted, wasCreated, bag, subslot, amount, totalCount);
		});

		return PacketParseResult::Pass;
	}

#ifdef MMO_WITH_DEV_COMMANDS
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
#endif

#ifdef MMO_WITH_DEV_COMMANDS
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
#endif


#ifdef MMO_WITH_DEV_COMMANDS
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
			guid = std::stoull(tokens[0]);
		}

		if (guid == 0)
		{
			ELOG("No target selected and no target guid provided to destroy!");
			return;
		}

		m_realmConnector.DestroyMonster(guid);
	}
#endif


#ifdef MMO_WITH_DEV_COMMANDS
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
#endif


#ifdef MMO_WITH_DEV_COMMANDS
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
#endif


#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_LevelUp(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		uint32 level = 1;
		if (!tokens.empty())
		{
			level = std::stoul(tokens[0]);
		}

		m_realmConnector.LevelUp(level);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_GiveMoney(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		if (tokens.empty())
		{
			ELOG("Usage: money <amount>");
			return;
		}

		const uint32 money = std::stoul(tokens[0]);
		m_realmConnector.GiveMoney(money);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_AddItem(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		if (tokens.empty())
		{
			ELOG("Usage: additem <item_id> [<count>]");
			return;
		}

		const uint32 itemId = std::stoul(tokens[0]);
		uint32 count = 1;

		if (tokens.size() > 1)
		{
			count = std::stoul(tokens[1]);
		}

		m_realmConnector.AddItem(itemId, count);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_WorldPort(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		if (tokens.empty())
		{
			ELOG("Usage: worldport <map_id> [<x>] [<y>] [<z>] [<facing degree>]");
			return;
		}

		Vector3 position = m_playerController->GetControlledUnit()->GetPosition();
		Radian facing = m_playerController->GetControlledUnit()->GetFacing();

		const uint32 mapId = std::stoul(tokens[0]);

		// Parse optional position
		if (tokens.size() > 1)
		{
			position.x = std::stof(tokens[1]);
		}
		if (tokens.size() > 2)
		{
			position.y = std::stof(tokens[2]);
		}
		if (tokens.size() > 3)
		{
			position.z = std::stof(tokens[3]);
		}

		if (tokens.size() > 4)
		{
			// Convert facing degree to radian
			const Degree facingDegree(std::stof(tokens[4]));
			facing = facingDegree;
		}

		m_realmConnector.WorldPort(mapId, position, facing);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_Speed(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		if (tokens.empty())
		{
			ELOG("Usage: speed <speed>");
			return;
		}

		const float speed = std::min(50.0f, std::stof(tokens[0]));
		if (speed <= 0.0f)
		{
			ELOG("Speed must be greater than 0!");
			return;
		}

		m_realmConnector.SetSpeed(speed);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_Summon(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		if (tokens.empty())
		{
			ELOG("Usage: summon <playername>");
			return;
		}

		if (tokens[0].empty())
		{
			ELOG("Player name cannot be empty!");
			return;
		}

		m_realmConnector.SummonPlayer(tokens[0]);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_Port(const std::string& cmd, const std::string& args) const
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(token);
		}

		if (tokens.empty())
		{
			ELOG("Usage: port <playername>");
			return;
		}

		if (tokens[0].empty())
		{
			ELOG("Player name cannot be empty!");
			return;
		}

		m_realmConnector.TeleportToPlayer(tokens[0]);
	}
#endif

	bool WorldState::LoadMap()
	{
		m_worldInstance.reset();

		const proto_client::MapEntry* map = m_project.maps.getById(g_mapId);
		ASSERT(map);

		const std::string assetPath = "Worlds/" + map->directory() + "/" + map->directory();
		DLOG("Loading map " << assetPath << ".hwld...");

		m_worldInstance = std::make_unique<ClientWorldInstance>(*m_scene, *m_worldRootNode, assetPath);

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

	void WorldState::OnItemPushCallback(const ItemInfo& itemInfo, uint64 characterGuid, bool wasLooted, bool wasCreated, uint8 bag, uint8 subslot, uint16 amount, uint16 totalCount)
	{
		// TODO: UI events for chat display
		if (characterGuid == ObjectMgr::GetActivePlayerGuid())
		{
			if (wasLooted)
			{
				DLOG("You looted item " << itemInfo.name << " x" << amount);
			}
			else
			{
				DLOG("You received item " << itemInfo.name << " x" << amount);
			}
		}
		else
		{
			std::shared_ptr<GameUnitC> unit = ObjectMgr::Get<GameUnitC>(characterGuid);
			ASSERT(unit);

			if (wasLooted)
			{
				DLOG(unit->GetName() << " looted item " << itemInfo.name << " x" << amount);
			}
			else
			{
				DLOG(unit->GetName() << " received item " << itemInfo.name << " x" << amount);
			}
		}
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
			auto* page = m_worldInstance->GetTerrain()->GetPage(pos.x(), pos.y());
			if (!page)
			{
				return;
			}

			if (isAvailable)
			{
				page->Prepare();
				EnsurePageIsLoaded(pos);
			}
			else
			{
				page->Unload();
			}
		}
	}

	PagePosition WorldState::GetPagePositionFromCamera() const
	{
		ASSERT(m_playerController);

		const auto& camPos = m_playerController->GetCamera().GetDerivedPosition();
		return PagePosition(static_cast<uint32>(
			floor(camPos.x / terrain::constants::PageSize)) + 32,
			static_cast<uint32>(floor(camPos.z / terrain::constants::PageSize)) + 32);
	}

	void WorldState::EnsurePageIsLoaded(PagePosition pos)
	{
		auto* page = m_worldInstance->GetTerrain()->GetPage(pos.x(), pos.y());
		if (!page || !page->IsLoadable())
		{
			// Page not known, already loaded or not loadable yet
			return;
		}

		const bool loaded = page->Load();
		if (!loaded)
		{
			m_dispatcher.post([pos, this]()
				{
					EnsurePageIsLoaded(pos);
				});
		}
	}

	void WorldState::OnTargetHealthChanged(uint64 monitoredGuid)
	{
		FrameManager::Get().TriggerLuaEvent("UNIT_HEALTH_UPDATED", "target");
	}

	void WorldState::OnTargetPowerChanged(uint64 monitoredGuid)
	{
		FrameManager::Get().TriggerLuaEvent("UNIT_POWER_UPDATED", "target");
	}

	void WorldState::OnTargetLevelChanged(uint64 monitoredGuid)
	{
		FrameManager::Get().TriggerLuaEvent("UNIT_LEVEL_UPDATED", "target");
	}

	void WorldState::OnRenderShadowsChanged(ConsoleVar& var, const std::string& oldValue)
	{
		if (m_sunLight)
		{
			m_sunLight->SetCastShadows(var.GetBoolValue());
		}
	}

	void WorldState::OnShadowBiasChanged(ConsoleVar& var, const std::string& oldValue)
	{
		WorldFrame* worldFrame = WorldFrame::GetWorldFrame();
		if (!worldFrame)
		{
			WLOG("World frame not found");
			return;
		}

		const WorldRenderer* renderer = reinterpret_cast<const WorldRenderer*>(worldFrame->GetRenderer());
		if (!renderer)
		{
			WLOG("World frame has no renderer");
			return;
		}

		DeferredRenderer* deferred = renderer->GetDeferredRenderer();
		if (!deferred)
		{
			WLOG("Deferred renderer not initialized");
			return;
		}

		DLOG("Updating depth bias values");
		deferred->SetDepthBias(s_depthBiasVar->GetFloatValue(),
			s_slopeDepthBiasVar->GetFloatValue(),
			s_clampDepthBiasVar->GetFloatValue());
	}

	void WorldState::GetPlayerName(uint64 guid, std::weak_ptr<GamePlayerC> player)
	{
		m_cache.GetNameCache().Get(guid, [player](uint64, const String& name)
		{
			if (const std::shared_ptr<GamePlayerC> strong = player.lock())
			{
				strong->SetName(name);
			}
		});
	}

	void WorldState::GetCreatureData(uint64 guid, std::weak_ptr<GameUnitC> creature)
	{
		m_cache.GetCreatureCache().Get(guid, [creature](uint64, const CreatureInfo& data)
		{
			if (const std::shared_ptr<GameUnitC> strong = creature.lock())
			{
				strong->SetCreatureInfo(data);
			}
		});
	}

	void WorldState::GetItemData(uint64 guid, std::weak_ptr<GameItemC> item)
	{
		m_cache.GetItemCache().Get(guid, [item](uint64, const ItemInfo& data)
			{
				if (const std::shared_ptr<GameItemC> strong = item.lock())
				{
					strong->NotifyItemData(data);
				}
			});
	}

	void WorldState::GetItemData(uint64 guid, std::weak_ptr<GamePlayerC> player)
	{
		m_cache.GetItemCache().Get(guid, [player](uint64, const ItemInfo& data)
			{
				if (const std::shared_ptr<GamePlayerC> strong = player.lock())
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
			closestHeight = std::max(terrainHeight, closestHeight);
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

					closestHeight = std::max(hitPoint.y, closestHeight);
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
		for (const Entity* entity : m_scene->GetAllEntities())
		{
			if ((entity->GetQueryFlags() & 1) == 0)
			{
				continue;
			}

			if (!entity->GetMesh() || entity->GetMesh()->GetCollisionTree().IsEmpty())
			{
				continue;
			}

			const AABB& entityAABB = entity->GetWorldBoundingBox();
			if (!entityAABB.Intersects(aabb))
			{
				continue;
			}

			out_potentialEntities.push_back(entity);
		}
	}

	void WorldState::OnMoveFallLand(GameUnitC& unit)
	{
		ASSERT(m_playerController);

		if (&unit != m_playerController->GetControlledUnit().get())
		{
			return;
		}

		m_playerController->OnMoveFallLand();
	}

	void WorldState::OnMoveFall(GameUnitC& unit)
	{
		ASSERT(m_playerController);

		if (&unit != m_playerController->GetControlledUnit().get())
		{
			return;
		}

		m_playerController->OnMoveFall();
	}

	void WorldState::SetSelectedTarget(uint64 guid)
	{
		m_targetObservers.disconnect();

		FrameManager::Get().TriggerLuaEvent("PLAYER_TARGET_CHANGED");

		if (guid != 0)
		{
			if (const auto targetUnit = ObjectMgr::Get<GameUnitC>(guid))
			{
				// Yes, register field observers
				m_targetObservers += targetUnit->RegisterMirrorHandler(object_fields::MaxHealth, 2, *this, &WorldState::OnTargetHealthChanged);
				m_targetObservers += targetUnit->RegisterMirrorHandler(object_fields::Mana, 7, *this, &WorldState::OnTargetPowerChanged);
				m_targetObservers += targetUnit->RegisterMirrorHandler(object_fields::Level, 2, *this, &WorldState::OnTargetLevelChanged);
			}
		}

		m_realmConnector.SetSelection(guid);
	}

	void WorldState::OnGuildChanged(std::weak_ptr<GamePlayerC> player, uint64 guildGuid)
	{
		if (guildGuid == 0)
		{
			return;
		}

		m_cache.GetGuildCache().Get(guildGuid, [player, this](uint64, const GuildInfo& info)
			{
				if (const auto strong = player.lock())
				{
					strong->NotifyGuildInfo(&info);
				}
			});
	}

	bool WorldState::GetGroundNormalAt(const Vector3& position, float range, Vector3& out_normal)
	{
		// First, check if terrain is present and provides a normal.
		if (m_worldInstance->HasTerrain())
		{
			// Assuming your terrain has a method GetNormalAt(x, z).
			out_normal = m_worldInstance->GetTerrain()->GetSmoothNormalAt(position.x, position.z);
			return true;
		}

		// Otherwise, use a ray query similar to GetHeightAt.
		Ray groundDetectionRay(position, position + Vector3::NegativeUnitY * range);
		m_rayQuery->ClearResult();
		m_rayQuery->SetRay(groundDetectionRay);
		m_rayQuery->SetQueryMask(1);
		const RaySceneQueryResult& result = m_rayQuery->Execute();

		bool foundIntersection = false;
		Vector3 bestNormal;
		float bestHitDistance = range;

		for (const auto& entry : result)
		{
			Entity* entity = dynamic_cast<Entity*>(entry.movable);
			if (!entity || !entity->GetMesh())
				continue;

			const AABBTree& collisionTree = entity->GetMesh()->GetCollisionTree();
			if (collisionTree.IsEmpty())
				continue;

			// Transform ray into the entity's local space.
			Matrix4 inverse = entity->GetParentNodeFullTransform().Inverse();
			Ray transformedRay(inverse * groundDetectionRay.origin,
				inverse * groundDetectionRay.destination);
			transformedRay.hitDistance = range;

			Vector3 localHitNormal;
			// Call the extended intersection function that outputs the hit normal.
			if (collisionTree.IntersectRay(transformedRay, nullptr, raycast_flags::EarlyExit, &localHitNormal))
			{
				float hitDistance = transformedRay.hitDistance;
				if (hitDistance < bestHitDistance)
				{
					bestHitDistance = hitDistance;
					// Transform the hit normal back to world space.
					bestNormal = entity->GetParentNodeFullTransform() * localHitNormal;
					foundIntersection = true;
				}
			}
		}

		if (foundIntersection)
		{
			out_normal = bestNormal.NormalizedCopy();
			return true;
		}

		return false;
	}

	void WorldState::GetObjectData(uint64 guid, std::weak_ptr<GameWorldObjectC_Base> object)
	{
		m_cache.GetObjectCache().Get(guid, [object](uint64, const ObjectInfo& data)
			{
				if (const std::shared_ptr<GameWorldObjectC_Base> strong = object.lock())
				{
					strong->NotifyObjectData(data);
				}
			});
	}
}
