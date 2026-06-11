// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_state.h"
#include "client.h"
#include "systems/loot_client.h"
#include "systems/trade_client.h"

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

#include <algorithm>
#include <charconv>
#include <vector>
#include <zstr/zstr.hpp>
#include <zlib.h>

#include "systems/action_bar.h"
#include "systems/quest_client.h"
#include "systems/cooldown_manager.h"
#include "game_client/object_mgr.h"
#include "game_common/projectile_target.h"
#include "systems/trainer_client.h"
#include "systems/vendor_client.h"
#include "world_deserializer.h"
#include "scene_graph/instanced_foliage.h"
#include "base/erase_by_move.h"
#include "base/profiler.h"
#include "base/timer_queue.h"
#include "frame_ui/text_component.h"
#include "frame_ui/localizer.h"
#include "game/aura.h"
#include "game/auto_attack.h"
#include "game/chat_type.h"
#include "game/damage_school.h"
#include "game_client/game_player_c.h"
#include "game/spell_target_map.h"
#include "game_client/game_item_c.h"
#include "ui/world_text_frame.h"
#include "game/quest.h"
#include "game_client/game_bag_c.h"
#include "terrain/page.h"
#include "terrain/constants.h"
#include "terrain/terrain.h"
#include "systems/guild_client.h"
#include "systems/friend_client.h"
#include "systems/talent_client.h"
#include "game/object_info.h"
#include "game/guild_info.h"

#include "shared/audio/audio.h"
#include "systems/party_info.h"
#include "shared/game_client/spell_visualization_service.h"
#include "console/console_var.h"
#include "frame_ui/font_mgr.h"
#include "game/group.h"
#include "game_client/game_world_object_c_base.h"
#include "graphics/texture_mgr.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/octree_scene.h"

#include "discord.h"
#include "loading_screen.h"
#include "systems/inventory_client.h"

#include "ui/minimap.h"
#include "world_ping_visualizer.h"

#include "luabind_lambda.h"
#include "luabind/luabind.hpp"
#include "scene_graph/movable_object.h"

namespace mmo
{
	const std::string WorldState::Name = "world";

	std::string s_zoneName = "Unknown";
	std::string s_subZoneName = "";

	extern uint32 g_mapId;

	// Console command names
	namespace command_names
	{
		static const char *s_toggleAxis = "ToggleAxis";
		static const char *s_toggleGrid = "ToggleGrid";
		static const char *s_toggleWire = "ToggleWire";
		static const char *s_toggleTerrainLOD = "ToggleTerrainLOD";
		static const char *s_toggleTerrainDebug = "ToggleTerrainDebug";
		static const char *s_freezeCulling = "ToggleCullingFreeze";
		static const char *s_reload = "reload";
	}

	namespace
	{
		static ConsoleVar *s_debugOutputPathVar = nullptr;

		static ConsoleVar *s_renderShadowsVar = nullptr;

		static ConsoleVar *s_depthBiasVar = nullptr;
		static ConsoleVar *s_slopeDepthBiasVar = nullptr;
		static ConsoleVar *s_clampDepthBiasVar = nullptr;
		static ConsoleVar *s_shadowTextureSizeVar = nullptr;
		static ConsoleVar *s_shadowQualityVar = nullptr;
		static ConsoleVar *s_shadowTemporalVar = nullptr;

		static ConsoleVar *s_renderScaleVar = nullptr;

		static ConsoleVar *s_depthPrepassVar = nullptr;

		static ConsoleVar *s_viewDistanceVar = nullptr;

		static ConsoleVar *s_foliageEnabledVar = nullptr;
		static ConsoleVar *s_foliageDensityVar = nullptr;

		static ConsoleVar *s_terrainLodEnabledVar = nullptr;
		static ConsoleVar *s_terrainOcclusionCullingVar = nullptr;

		String MapMouseButton(const MouseButton button)
		{
			if ((button & MouseButton::Left) == MouseButton::Left)
				return "LMB";
			if ((button & MouseButton::Right) == MouseButton::Right)
				return "RMB";
			if ((button & MouseButton::Middle) == MouseButton::Middle)
				return "MMB";

			return {};
		}

		// Windows virtual key code constants (matches VK_* values from winuser.h)
		enum : Key
		{
			Key_Backspace    = 0x08,
			Key_Tab          = 0x09,
			Key_Enter        = 0x0D,
			Key_Shift        = 0x10,
			Key_Control      = 0x11,
			Key_Pause        = 0x13,
			Key_Accept       = 0x1E,
			Key_Escape       = 0x1B,
			Key_Space        = 0x20,
			Key_PageUp       = 0x21,
			Key_PageDown     = 0x22,
			Key_End          = 0x23,
			Key_Home         = 0x24,
			Key_Left         = 0x25,
			Key_Up           = 0x26,
			Key_Right        = 0x27,
			Key_Down         = 0x28,
			Key_PrintScreen  = 0x2C,
			Key_Insert       = 0x2D,
			Key_Delete       = 0x2E,
			Key_NumPad0      = 0x60,
			Key_NumPad9      = 0x69,
			Key_Multiply     = 0x6A,
			Key_Add          = 0x6B,
			Key_Subtract     = 0x6D,
			Key_Divide       = 0x6F,
			Key_F1           = 0x70,
			Key_F24          = 0x87,
			Key_ScrollLock   = 0x91,
		};

		String MapBindingKeyCode(const Key keyCode)
		{
			if (keyCode >= Key_F1 && keyCode <= Key_F24)
			{
				return String("F") + std::to_string(keyCode - Key_F1 + 1);
			}

			if ((keyCode >= 'A' && keyCode <= 'Z') || (keyCode >= '0' && keyCode <= '9'))
			{
				return String(1, static_cast<char>(keyCode));
			}

			if (keyCode >= Key_NumPad0 && keyCode <= Key_NumPad9)
			{
				return String("NUM-") + String(1, static_cast<char>(keyCode - Key_NumPad0 + '0'));
			}

			switch (keyCode)
			{
			case Key_Space:       return "SPACE";
			case Key_Enter:       return "ENTER";
			case Key_Escape:      return "ESCAPE";
			case Key_Backspace:   return "BACKSPACE";
			case Key_Tab:         return "TAB";
			case Key_Add:         return "ADD";
			case Key_Subtract:    return "SUBTRACT";
			case Key_Multiply:    return "MULTIPLY";
			case Key_Divide:      return "DIVIDE";
			case Key_Accept:      return "ACCEPT";
			case Key_Delete:      return "DEL";
			case Key_Insert:      return "INSERT";
			case Key_Control:     return "CONTROL";
			case Key_Shift:       return "SHIFT";
			case Key_Left:        return "LEFT";
			case Key_Right:       return "RIGHT";
			case Key_Up:          return "UP";
			case Key_Down:        return "DOWN";
			case Key_PageUp:      return "PAGEUP";
			case Key_PageDown:    return "PAGEDOWN";
			case Key_End:         return "END";
			case Key_Home:        return "HOME";
			case Key_PrintScreen: return "PRINTSCREEN";
			case Key_ScrollLock:  return "SCROLLLOCK";
			case Key_Pause:       return "PAUSE";
			default:              break;
			}

			if (keyCode >= 'a' && keyCode <= 'z')
			{
				return String(1, static_cast<char>(keyCode - 'a' + 'A'));
			}

			return {};
		}
	}

	IInputControl *WorldState::s_inputControl = nullptr;

	WorldState::WorldState(GameStateMgr &gameStateManager, RealmConnector &realmConnector, const proto_client::Project &project, TimerQueue &timers, LootClient &lootClient, VendorClient &vendorClient,
						   ActionBar &actionBar, SpellCast &spellCast, CooldownManager &cooldownManager, TrainerClient &trainerClient, QuestClient &questClient, IAudio &audio, PartyInfo &partyInfo, CharSelect &charSelect, GuildClient &guildClient, FriendClient &friendClient, ICacheProvider &cache, Discord &discord,
						   GameTimeComponent &gameTime, TalentClient &talentClient, Minimap &minimap, InventoryClient &inventoryClient, TradeClient &tradeClient)
		: GameState(gameStateManager)
		, m_realmConnector(realmConnector)
		, m_audio(audio)
		, m_gameTime(gameTime)
		, m_cache(cache)
		, m_project(project)
		, m_timers(timers)
		, m_lootClient(lootClient)
		, m_vendorClient(vendorClient)
		, m_actionBar(actionBar)
		, m_spellCast(spellCast)
		, m_cooldownManager(cooldownManager)
		, m_trainerClient(trainerClient)
		, m_questClient(questClient)
		, m_partyInfo(partyInfo)
		, m_charSelect(charSelect)
		, m_guildClient(guildClient)
		, m_friendClient(friendClient)
		, m_discord(discord)
		, m_talentClient(talentClient)
		, m_minimap(minimap)
		, m_inventoryClient(inventoryClient)
		, m_tradeClient(tradeClient)
	{
		// TODO: Do we want to put these asset references in some sort of config setting or something?
		ObjectMgr::SetUnitNameFontSettings(FontManager::Get().CreateOrRetrieve("Fonts/FRIZQT__.TTF", 24.0f, 1.0f), MaterialManager::Get().Load("Models/UnitNameFont.hmat"));
	}

	void WorldState::OnEnter()
	{
		LoadingScreen::Show();

		ObjectMgr::Initialize(m_project, m_partyInfo);

		// Initialize spell visualization service with direct audio access
		SpellVisualizationService::Get().Initialize(m_project, &m_audio);

		SetupWorldScene();

		// Create projectile manager
		m_projectileManager = std::make_unique<ProjectileManager>(*m_scene, &m_audio);

		// Connect projectile impact signal to visualization service
		m_projectileManager->projectileImpact.connect([](const proto_client::SpellEntry &spell, IProjectileTarget *target)
													  {
				std::vector<GameUnitC*> targets;
				if (target)
				{
					// Try to get the GameUnitC from the target's GUID
					if (auto unit = ObjectMgr::Get<GameUnitC>(target->GetGuid()))
					{
						targets.push_back(unit.get());
					}
				}
				SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::Impact, spell, nullptr, targets); });

		m_debugPathVisualizer = std::make_unique<DebugPathVisualizer>(*m_scene);
		m_worldPingVisualizer = std::make_unique<WorldPingVisualizer>(*m_scene);

		// Register world renderer
		FrameManager::Get().RegisterFrameRenderer("WorldRenderer", [this](const std::string &name)
												  { return std::make_unique<WorldRenderer>(name, *m_scene); });

		// Register world frame type
		FrameManager::Get().RegisterFrameFactory("World", [](const std::string &name)
												 {
			auto worldFrame = std::make_shared<WorldFrame>(name);
			worldFrame->SetAsCurrentWorldFrame();
			return worldFrame; });

		ReloadUI();

		// Load bindings
		m_bindings.Initialize(*m_playerController);
		m_bindings.Load("Interface/Bindings.xml");

		m_realmConnections += {
			m_realmConnector.EnterWorldFailed.connect(*this, &WorldState::OnEnterWorldFailed),
			m_realmConnector.Disconnected.connect(*this, &WorldState::OnRealmDisconnected)};

		const auto *selectedCharacter = m_charSelect.GetCharacterView(m_charSelect.GetSelectedCharacter());
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
		m_paintLayer = Screen::AddLayer([this]
										{ OnPaint(); }, 1.0f, ScreenLayerFlags::IdentityTransform);

		m_inputConnections += {
			EventLoop::MouseDown.connect(this, &WorldState::OnMouseDown),
			EventLoop::MouseUp.connect(this, &WorldState::OnMouseUp),
			EventLoop::MouseMove.connect(this, &WorldState::OnMouseMove),
			EventLoop::KeyDown.connect(this, &WorldState::OnKeyDown),
			EventLoop::MouseWheel.connect(this, &WorldState::OnMouseWheel),
			EventLoop::KeyUp.connect(this, &WorldState::OnKeyUp),
			EventLoop::Idle.connect(this, &WorldState::OnIdle)};

		RegisterGameplayCommands();

		SetupPacketHandler();

		// TODO: Remove me. We abuse this here for preloading the font
		WorldTextFrame frame(m_playerController->GetCamera(), Vector3::Zero, 0.0f);
		frame.GetFont()->GetTextWidth("1");

		// Ensure fog is enabled
		m_scene->SetFogEnabled(true);

		m_worldRootNode = m_scene->GetRootSceneNode().CreateChildSceneNode();
		LoadMap();

		// Play background music
		m_backgroundMusicSound = m_audio.CreateLoopedStream("Sound/Music/Gethsemane.ogg");
		m_audio.PlaySound(m_backgroundMusicSound, &m_backgroundMusicChannel);

		// Play ambience
		m_ambienceSound = m_audio.CreateSound("Sound/Ambience/ZoneAmbience/ForestNormalDay.wav", SoundType::SoundLooped2D);
		m_audio.PlaySound(m_ambienceSound, &m_ambienceChannel);

		// Initialize rich presence
		const mmo::CharacterView *character = m_charSelect.GetCharacterView(m_charSelect.GetSelectedCharacter());
		ASSERT(character);
		if (character)
		{
			const proto_client::RaceEntry *race = m_project.races.getById(character->GetRaceId());
			const proto_client::ClassEntry *classEntry = m_project.classes.getById(character->GetClassId());
			m_discord.NotifyCharacterData(
				character->GetName(),
				character->GetLevel(),
				classEntry ? classEntry->name() : "UNKNOWN",
				race ? race->name() : "UNKNOWN");
		}
	}

	void WorldState::OnLeave()
	{
		m_worldPingVisualizer.reset();
		m_debugPathVisualizer.reset();
		m_foliage.reset();

		auto stopAudio = [this](SoundIndex &sound, ChannelIndex &channel)
		{
			m_audio.StopSound(&channel);
			channel = InvalidChannel;
			sound = InvalidSound;
		};
		stopAudio(m_backgroundMusicSound, m_backgroundMusicChannel);
		stopAudio(m_ambienceSound, m_ambienceChannel);

		m_rayQuery.reset();

		// Stop background loading thread
		m_work.reset();
		m_workQueue.stop();
		m_dispatcher.stop();
		m_backgroundLoader.join();

		m_workQueue.reset();
		m_dispatcher.reset();

		if (m_projectileManager)
		{
			m_projectileManager->Clear();
			m_projectileManager.reset();
		}

		ObjectMgr::Initialize(m_project, m_partyInfo);

		m_worldInstance.reset();
		RemovePacketHandler();

		RemoveGameplayCommands();

		s_inputControl = nullptr;
		m_playerController.reset();
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

		// Go back to realm list
		m_discord.NotifyRealmChanged(m_realmConnector.GetRealmName());

		// Ensure the loading screen is not visible anymore in any case
		LoadingScreen::Hide();
	}

	std::string_view WorldState::GetName() const
	{
		return Name;
	}

	void WorldState::ReloadUI()
	{
		// Reset the logo frame ui
		FrameManager::Get().ResetTopFrame();

		// Make the top frame element
		const auto topFrame = FrameManager::Get().CreateOrRetrieve("Frame", "TopGameFrame");
		topFrame->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr);
		topFrame->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr);
		topFrame->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr);
		topFrame->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr);
		FrameManager::Get().SetTopFrame(topFrame);

		// Load ui file
		FrameManager::Get().LoadUIFile("Interface/GameUI/GameUI.toc");

		// Override/extend Lua globals that need WorldState context
		RegisterWorldLuaFunctions();

		if (ObjectMgr::GetActivePlayerGuid())
		{
			FrameManager::Get().TriggerLuaEvent("PLAYER_ENTER_WORLD");
		}
	}

	void WorldState::RegisterWorldLuaFunctions()
	{
		lua_State* L = FrameManager::Get().GetLuaState();
		if (!L)
		{
			return;
		}

		luabind::module(L)
		[
			// Override SendPing to do hovered-object check + terrain raycast before sending
			luabind::def<std::function<void()>>("SendPing", [this]()
			{
				if (!m_playerController || !m_scene)
				{
					return;
				}

				// 1) Check hovered object (unit or game object)
				GameObjectC* hovered = m_playerController->GetHoveredObject();
				if (hovered && hovered->IsUnit())
				{
					m_realmConnector.SendPartyPingUnit(hovered->GetGuid());
					return;
				}

				// 2) Terrain/collidable raycast using current mouse position
				if (m_rayQuery)
				{
					const int32 mouseX = m_playerController->GetMouseX();
					const int32 mouseY = m_playerController->GetMouseY();

					int32 screenW = 1920, screenH = 1080;
					GraphicsDevice::Get().GetViewport(nullptr, nullptr, &screenW, &screenH);

					const float nx = static_cast<float>(mouseX) / static_cast<float>(screenW);
					const float ny = static_cast<float>(mouseY) / static_cast<float>(screenH);

					const Ray mouseRay = m_playerController->GetCamera().GetCameraToViewportRay(nx, ny, 1000.0f);

					m_rayQuery->ClearResult();
					m_rayQuery->SetSortByDistance(true);
					m_rayQuery->SetQueryMask(1 << 6); // terrain + world models
					m_rayQuery->SetRay(mouseRay);
					m_rayQuery->Execute();

					const auto& results = m_rayQuery->GetLastResult();
					for (const auto& result : results)
					{
						if (!result.movable)
						{
							continue;
						}

						const ICollidable* collidable = result.movable->GetCollidable();
						if (!collidable || !collidable->IsCollidable())
						{
							continue;
						}

						CollisionResult collision;
						if (collidable->TestRayCollision(mouseRay, collision))
						{
							const Vector3 hitPoint = mouseRay.origin + mouseRay.GetDirection() * collision.penetrationDepth;
							m_realmConnector.SendPartyPingPosition(hitPoint.x, hitPoint.y, hitPoint.z);
							return;
						}
					}
				}

				// 3) Fallback: ping at player position
				auto player = ObjectMgr::GetActivePlayer();
				if (player)
				{
					const Vector3& pos = player->GetPosition();
					m_realmConnector.SendPartyPingPosition(pos.x, pos.y, pos.z);
				}
			})
		];
	}

	void WorldState::OnTargetSelectionChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);

		// DLOG("Target changed to " << log_hex_digit(ObjectMgr::GetActivePlayer()->Get<uint64>(object_fields::TargetUnit)));
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

	void WorldState::OnPlayerGuildChanged(uint64 monitoredGuid)
	{
		ASSERT(ObjectMgr::GetActivePlayerGuid() == monitoredGuid);
		m_guildClient.NotifyGuildChanged(ObjectMgr::GetActivePlayer()->Get<uint64>(object_fields::Guild));
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

		if (m_debugPathVisualizer)
		{
			m_debugPathVisualizer->Update(deltaSeconds);
		}

		if (m_worldPingVisualizer && m_playerController)
		{
			m_worldPingVisualizer->Update(deltaSeconds, m_playerController->GetCamera());
		}

		for (size_t i = 0; i < 20; ++i)
		{
			if (!m_dispatcher.poll_one())
			{
				break;
			}
		}

		// Update sky component (handles day/night cycle and lighting)
		m_skyComponent->Update(deltaSeconds, timestamp);

		// Set the sky dome position to follow the player
		if (m_playerController->GetRootNode())
		{
			m_skyComponent->SetPosition(m_playerController->GetRootNode()->GetPosition());
		}

		// Update audio component to simulate 3d audio correctly from the player position
		if (m_playerController->GetControlledUnit())
		{
			m_audio.Update(m_playerController->GetControlledUnit()->GetPosition(), deltaSeconds);
		}

		// Only update when world loading is ready
		if (m_worldLoaded && m_timeSyncResponseSent)
		{
			m_playerController->Update(deltaSeconds);
			ObjectMgr::UpdateObjects(deltaSeconds);
		}

		// Update cooldown manager
		m_cooldownManager.Update(deltaSeconds);

		// Update minimap
		if (const auto &controlled = m_playerController->GetControlledUnit())
		{
			m_minimap.UpdatePlayerPosition(controlled->GetPosition(), controlled->GetFacing());

			// Gather visible party member positions for minimap dots
			std::vector<Minimap::PartyMemberDot> partyPositions;
			const uint32 memberCount = m_partyInfo.GetMemberCount();
			const uint64 selfGuid = controlled->GetGuid();
			for (uint32 i = 0; i < memberCount; ++i)
			{
				const int32 idx = static_cast<int32>(i);
				const uint64 memberGuid = m_partyInfo.GetMemberGuid(idx);
				if (memberGuid == selfGuid)
				{
					continue;
				}
				if (const auto memberUnit = ObjectMgr::Get<GameUnitC>(memberGuid))
				{
					const auto* member = m_partyInfo.GetMember(idx);
					partyPositions.push_back({ memberUnit->GetPosition(), member ? member->name : memberUnit->GetName() });
				}
			}
			m_minimap.UpdatePartyPositions(std::move(partyPositions));

			// Gather quest giver dots for minimap
			std::vector<Minimap::QuestGiverDot> questDots;
			ObjectMgr::ForEachUnit([&questDots](GameUnitC& unit)
			{
				const QuestgiverStatus status = unit.GetQuestGiverStatus();
				Minimap::QuestDotType dotType;
				switch (status)
				{
				case questgiver_status::Available:
				case questgiver_status::AvailableRep:
					dotType = Minimap::QuestDotType::Available;
					break;
				case questgiver_status::Reward:
				case questgiver_status::RewardRep:
					dotType = Minimap::QuestDotType::Completable;
					break;
				default:
					return; // Not shown on minimap
				}
				questDots.push_back({ unit.GetPosition(), dotType, unit.GetName() });
			});
			m_minimap.UpdateQuestGiverDots(std::move(questDots));
		}

		// Tick ping timers and remove expired ones
		if (!m_activePings.empty())
		{
			for (auto& ping : m_activePings)
			{
				ping.remainingTime -= deltaSeconds;
			}
			m_activePings.erase(
				std::remove_if(m_activePings.begin(), m_activePings.end(),
					[](const Minimap::PingDot& p) { return p.remainingTime <= 0.0f; }),
				m_activePings.end());
			m_minimap.UpdatePings(m_activePings);
		}

		// Update projectiles
		if (m_projectileManager)
		{
			m_projectileManager->Update(deltaSeconds);
		}

		// Update spell visualization service (audio fading, etc.)
		SpellVisualizationService::Get().Update(deltaSeconds);

		// Update pending projectiles (animation notify fallback)
		UpdatePendingProjectiles();

		// Update foliage system
		if (m_foliage && m_worldLoaded)
		{
			m_foliage->Update(m_playerController->GetCamera());
		}

		// Cull distant authored instanced foliage (trees) by the configured view distance. Applying
		// the cvar value here every frame keeps it correct regardless of page streaming / world
		// reloads, and the cull itself only touches chunks whose visibility actually changed.
		if (m_worldLoaded && m_worldInstance)
		{
			if (InstancedFoliage* trees = m_worldInstance->GetInstancedFoliage())
			{
				trees->SetViewDistance(s_viewDistanceVar ? s_viewDistanceVar->GetFloatValue() : 0.0f);
				trees->UpdateViewDistance(m_playerController->GetCamera().GetDerivedPosition());
			}
		}

		// Notify terrain of the current camera position so it can detect large single-frame
		// displacements (building exit, teleport) and reset stale tile occlusion state.
		if (m_worldLoaded && m_worldInstance->HasTerrain())
		{
			m_worldInstance->GetTerrain()->NotifyCameraPosition(
				m_playerController->GetCamera().GetDerivedPosition());
		}

		const auto pos = GetPagePositionFromCamera();
		m_memoryPointOfView->UpdateCenter(pos);
		m_visibleSection->UpdateCenter(pos);

		CheckForZoneUpdate();

		// Update world text frames then remove expired ones
		for (auto &frame : m_worldTextFrames)
		{
			frame->Update(deltaSeconds);
		}
		m_worldTextFrames.erase(
			std::remove_if(m_worldTextFrames.begin(), m_worldTextFrames.end(),
				[](const std::unique_ptr<WorldTextFrame> &frame) { return frame->IsExpired(); }),
			m_worldTextFrames.end());
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
		for (const auto &textFrame : m_worldTextFrames)
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

		const auto pos = unit->GetPosition();
		const uint32 zoneId = m_worldInstance->GetTerrain()->GetArea(pos);
		if (zoneId != m_lastZoneId)
		{
			m_lastZoneId = zoneId;

			const proto_client::ZoneEntry *zone = m_project.zones.getById(m_lastZoneId);
			if (zone)
			{
				const proto_client::ZoneEntry *parentZone =
					(zone->parentzone() != 0) ? m_project.zones.getById(zone->parentzone()) : nullptr;

				if (parentZone)
				{
					s_zoneName = parentZone->name();
					s_subZoneName = zone->name();
				}
				else
				{
					s_zoneName = zone->name();
					s_subZoneName.clear();
				}
			}
			else
			{
				s_zoneName = "Unknown";
			}

			m_discord.NotifyZoneChanged(s_subZoneName.empty() ? s_zoneName : s_zoneName + " - " + s_subZoneName);
			FrameManager::Get().TriggerLuaEvent("ZONE_CHANGED");
		}
	}

	void WorldState::SetupWorldScene()
	{
		m_scene = std::make_unique<OctreeScene>();
		m_scene->SetFogRange(60.0f, 500.0f);

		// Create sky component to manage the sky dome and day/night cycle
		m_skyComponent = std::make_unique<SkyComponent>(*m_scene, &m_gameTime);

		m_rayQuery = std::move(m_scene->CreateRayQuery(Ray()));
		m_rayQuery->SetSortByDistance(true);
		m_rayQuery->SetQueryMask(1);
		m_rayQuery->SetDebugHitTestResults(true);

		m_playerController = std::make_unique<PlayerController>(*m_scene, m_realmConnector, m_lootClient, m_vendorClient, m_trainerClient, m_spellCast);
		s_inputControl = m_playerController.get();

		// Create the world grid in the scene. The world grid component will handle the rest for us
		m_worldGrid = std::make_unique<WorldGrid>(*m_scene, "WorldGrid");
		m_worldGrid->SetVisible(false);

		// Debug axis object
		m_debugAxis = std::make_unique<AxisDisplay>(*m_scene, "WorldDebugAxis");
		m_scene->GetRootSceneNode().AddChild(m_debugAxis->GetSceneNode());
		m_debugAxis->SetVisible(false);

		// Ensure the work queue is always busy
		m_work = std::make_unique<asio::io_service::work>(m_workQueue);

		// Setup background loading thread
		auto &workQueue = m_workQueue;
		auto &dispatcher = m_dispatcher;
		m_backgroundLoader = std::thread([&workQueue]()
										 { workQueue.run(); });

		const auto addWork = [&workQueue](const WorldPageLoader::Work &work)
		{
			workQueue.post(work);
		};
		const auto synchronize = [&dispatcher](const WorldPageLoader::Work &work)
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
			*m_pageLoader);

		// Initialize foliage system
		SetupFoliage();
	}

	void WorldState::SetupFoliage()
	{
		m_foliage = std::make_unique<Foliage>(*m_scene, GraphicsDevice::Get());

		// Set up height query callback that checks terrain height, normal, and holes
		m_foliage->SetHeightQueryCallback([this](float x, float z, float& height, Vector3& normal) -> bool
		{
			// Check if world instance and terrain are available
			if (!m_worldInstance || !m_worldInstance->HasTerrain())
			{
				return false;
			}

			terrain::Terrain* terrain = m_worldInstance->GetTerrain();
			if (!terrain)
			{
				return false;
			}

			// Check if this position is a hole
			if (terrain->IsHoleAt(x, z))
			{
				return false;
			}

			// Get height and normal from terrain
			height = terrain->GetSmoothHeightAt(x, z);
			normal = terrain->GetSmoothNormalAt(x, z);

			// Check slope - if normal.y is too low, slope is too steep
			// cos(35°) ≈ 0.8191f, so normal.y must be >= 0.8191f for walkable terrain
			constexpr float maxSlopeCosine = 0.8191f; // 35 degrees
			if (normal.y < maxSlopeCosine)
			{
				return false;
			}

			// Only render foliage where terrain layer 0 has > 30% influence
			constexpr float minLayer0Influence = 0.3f;
			if (terrain->GetLayerValueAt(x, z, 0) < minLayer0Influence)
			{
				return false;
			}

			return true;
		});

		// Configure foliage settings
		FoliageSettings settings;
		settings.chunkSize = 32.0f;
		settings.maxViewDistance = 50.0f;
		settings.loadRadius = 3;
		settings.frustumCulling = true;
		settings.globalDensityMultiplier = 1.0f;
		m_foliage->SetSettings(settings);

		// Set bounds to cover the entire terrain (64x64 pages)
		// Terrain is centered at origin, so it extends from -halfSize to +halfSize
		constexpr float halfTerrainSize = 64.0f * terrain::constants::PageSize * 0.5f;
		m_foliage->SetBounds(AABB(
			Vector3(-halfTerrainSize, -1000.0f, -halfTerrainSize),
			Vector3(halfTerrainSize, 1000.0f, halfTerrainSize)
		));

		auto addFoliageLayer = [this](const char *name, const char *meshPath, float density, float minScale, float maxScale)
		{
			MeshPtr mesh = MeshManager::Get().Load(meshPath);
			if (!mesh)
			{
				return;
			}

			auto layer = std::make_shared<FoliageLayer>(name, mesh);
			FoliageLayerSettings &s = layer->GetSettings();
			s.density = density;
			s.minScale = minScale;
			s.maxScale = maxScale;
			s.maxSlopeAngle = 35.0f;
			s.fadeStartDistance = 40.0f;
			s.fadeEndDistance = 60.0f;
			s.castShadows = false;
			m_foliage->AddLayer(layer);
		};

		addFoliageLayer("Grass",    "Models/FalwynPlains/Plants/Grass_01.hmsh",       4.0f,  0.7f, 1.3f);
		addFoliageLayer("Grass02",  "Models/FalwynPlains/Plants/Grass_02.hmsh",       0.7f,  1.0f, 1.0f);
		addFoliageLayer("Flowers",  "Models/FalwynPlains/Plants/Shrub_Flower_01.hmsh",0.32f, 1.0f, 1.0f);
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
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStartSwim, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStopSwim, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStartWalk, *this, &WorldState::OnMovement);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStopWalk, *this, &WorldState::OnMovement);
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
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ChannelStart, *this, &WorldState::OnChannelStart);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ChannelUpdate, *this, &WorldState::OnChannelUpdate);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::AttackStart, *this, &WorldState::OnAttackStart);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::AttackStop, *this, &WorldState::OnAttackStop);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::AttackSwingError, *this, &WorldState::OnAttackSwingError);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::XpLog, *this, &WorldState::OnXpLog);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SpellDamageLog, *this, &WorldState::OnSpellDamageLog);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::NonSpellDamageLog, *this, &WorldState::OnNonSpellDamageLog);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::EnvironmentalDamageLog, *this, &WorldState::OnLogEnvironmentalDamage);

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
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::PartyPing, *this, &WorldState::OnPartyPing);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::RandomRollResult, *this, &WorldState::OnRandomRollResult);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::AttackerStateUpdate, *this, &WorldState::OnAttackerStateUpdate);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SpellHealLog, *this, &WorldState::OnSpellHealLog);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::LogoutResponse, *this, &WorldState::OnLogoutResponse);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MessageOfTheDay, *this, &WorldState::OnMessageOfTheDay);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveRoot, *this, &WorldState::OnMoveRoot);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveStun, *this, &WorldState::OnMoveStun);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveSleep, *this, &WorldState::OnMoveSleep);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveFear, *this, &WorldState::OnMoveFear);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MoveDisorient, *this, &WorldState::OnMoveDisorient);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::GameTimeInfo, *this, &WorldState::OnGameTimeInfo);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SetProficiency, *this, &WorldState::OnSetProficiency);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::SpellModChanged, *this, &WorldState::OnSpellModChanged);

		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::TimePlayedResponse, *this, &WorldState::OnTimePlayedResponse);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::TimeSyncRequest, *this, &WorldState::OnTimeSyncRequest);
		m_worldPacketHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::DebugLineOfSightResult, *this, &WorldState::OnDebugLineOfSightResult);

		m_lootClient.Initialize();
		m_vendorClient.Initialize();
		m_trainerClient.Initialize();
		m_questClient.Initialize();
		m_partyInfo.Initialize();
		m_guildClient.Initialize();
		m_friendClient.Initialize();
		m_talentClient.Initialize();
		m_inventoryClient.Initialize();
		m_tradeClient.Initialize();
#ifdef MMO_WITH_DEV_COMMANDS
		Console::RegisterCommand("createmonster", [this](const std::string &cmd, const std::string &args)
								 { Command_CreateMonster(cmd, args); }, ConsoleCommandCategory::Gm, "Spawns a monster from a specific id. The monster will not persist on server restart.");
		Console::RegisterCommand("destroymonster", [this](const std::string &cmd, const std::string &args)
								 { Command_DestroyMonster(cmd, args); }, ConsoleCommandCategory::Gm, "Destroys a spawned monster from a specific guid.");
		Console::RegisterCommand("learnspell", [this](const std::string &cmd, const std::string &args)
								 { Command_LearnSpell(cmd, args); }, ConsoleCommandCategory::Gm, "Makes the selected player learn a given spell.");
		Console::RegisterCommand("followme", [this](const std::string &cmd, const std::string &args)
								 { Command_FollowMe(cmd, args); }, ConsoleCommandCategory::Gm, "Makes the selected creature follow you.");
		Console::RegisterCommand("faceme", [this](const std::string &cmd, const std::string &args)
								 { Command_FaceMe(cmd, args); }, ConsoleCommandCategory::Gm, "Makes the selected creature face towards you.");
		Console::RegisterCommand("level", [this](const std::string &cmd, const std::string &args)
								 { Command_LevelUp(cmd, args); }, ConsoleCommandCategory::Gm, "Increases the targets level if possible.");
		Console::RegisterCommand("money", [this](const std::string &cmd, const std::string &args)
								 { Command_GiveMoney(cmd, args); }, ConsoleCommandCategory::Gm, "Increases the targets money.");
		Console::RegisterCommand("additem", [this](const std::string &cmd, const std::string &args)
								 { Command_AddItem(cmd, args); }, ConsoleCommandCategory::Gm, "Adds an item to the target players inventory.");
		Console::RegisterCommand("worldport", [this](const std::string &cmd, const std::string &args)
								 { Command_WorldPort(cmd, args); }, ConsoleCommandCategory::Gm, "Teleports the player to the given map, optionally also changing the position.");
		Console::RegisterCommand("port", [this](const std::string &cmd, const std::string &args)
								 { Command_Port(cmd, args); }, ConsoleCommandCategory::Gm, "Teleports you to the named player if he exists.");
		Console::RegisterCommand("summon", [this](const std::string &cmd, const std::string &args)
								 { Command_Summon(cmd, args); }, ConsoleCommandCategory::Gm, "Summons the named player to your location if such a player exists.");
		Console::RegisterCommand("speed", [this](const std::string &cmd, const std::string &args)
								 { Command_Speed(cmd, args); }, ConsoleCommandCategory::Gm, "Sets your movement speed to the new value in meters per second.");
		Console::RegisterCommand("checklos", [this](const std::string &cmd, const std::string &args)
								 { Command_CheckLineOfSight(cmd, args); }, ConsoleCommandCategory::Gm, "Performs a server-side line of sight check from your position to the selected target and visualizes the result.");
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
		Console::UnregisterCommand("checklos");
#endif

		m_tradeClient.Shutdown();
		m_inventoryClient.Shutdown();
		m_talentClient.Shutdown();
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

		// Go back to login state, flagging why we left
		LoginState::s_returnReason = LoginReturnReason::RealmDisconnected;
		GameStateMgr::Get().SetGameState(LoginState::Name);
	}

	void WorldState::OnEnterWorldFailed(game::player_login_response::Type error)
	{
		LoginState::s_returnReason = LoginReturnReason::EnterWorldFailed;
		GameStateMgr::Get().SetGameState(LoginState::Name);
	}

	void WorldState::RegisterGameplayCommands()
	{
		s_debugOutputPathVar = ConsoleVarMgr::RegisterConsoleVar("DebugTargetPath", "", "0");
		s_renderShadowsVar = ConsoleVarMgr::RegisterConsoleVar("RenderShadows", "Determines whether or not shadows should be rendered", "1");
		m_cvarChangedSignals += s_renderShadowsVar->Changed.connect(this, &WorldState::OnRenderShadowsChanged);

		s_depthBiasVar = ConsoleVarMgr::RegisterConsoleVar("ShadowDepthBias", "", "138.6");
		m_cvarChangedSignals += s_depthBiasVar->Changed.connect(this, &WorldState::OnShadowBiasChanged);
		s_slopeDepthBiasVar = ConsoleVarMgr::RegisterConsoleVar("ShadowSlopeBias", "", "0.1");
		m_cvarChangedSignals += s_slopeDepthBiasVar->Changed.connect(this, &WorldState::OnShadowBiasChanged);
		s_clampDepthBiasVar = ConsoleVarMgr::RegisterConsoleVar("ShadowClampBias", "", "0.0");
		m_cvarChangedSignals += s_clampDepthBiasVar->Changed.connect(this, &WorldState::OnShadowBiasChanged);

		s_shadowTextureSizeVar = ConsoleVarMgr::RegisterConsoleVar("ShadowTextureSize", "", "1");
		m_cvarChangedSignals += s_shadowTextureSizeVar->Changed.connect(this, &WorldState::OnShadowTextureSizeChanged);

		s_shadowQualityVar = ConsoleVarMgr::RegisterConsoleVar("ShadowQuality", "Shadow detail preset: 0 = Low (2 cascades, 4 PCF taps), 1 = Medium (3/8), 2 = High (4/16). Lower values improve performance.", "2");
		m_cvarChangedSignals += s_shadowQualityVar->Changed.connect(this, &WorldState::OnShadowQualityChanged);

		s_shadowTemporalVar = ConsoleVarMgr::RegisterConsoleVar("ShadowTemporal", "Temporal cascade staggering: 1 = distant cascades refresh every few frames (faster), 0 = every cascade every frame.", "1");
		m_cvarChangedSignals += s_shadowTemporalVar->Changed.connect(this, &WorldState::OnShadowTemporalChanged);

		// Internal 3D render-scale: the world is rendered at this fraction of the frame size and then
		// upscaled. 1.0 = native; lower values trade sharpness for a large performance gain on weak
		// GPUs. Read directly by WorldRenderer each frame, so no change handler is required here.
		s_renderScaleVar = ConsoleVarMgr::RegisterConsoleVar("gxRenderScale", "3D render resolution scale (0.25 to 1.0). Lower values improve performance by rendering the world at a lower resolution and upscaling.", "1.0");

		s_depthPrepassVar = ConsoleVarMgr::RegisterConsoleVar("gxDepthPrepass", "Render an opaque depth pre-pass before the G-Buffer pass. Reduces overdraw shading cost in scenes with heavy opaque overdraw (e.g. dense foliage). 1 = on, 0 = off.", "0");
		m_cvarChangedSignals += s_depthPrepassVar->Changed.connect(this, &WorldState::OnDepthPrepassChanged);

		// Distance (world units) beyond which authored instanced foliage (trees, bushes, rocks) is
		// culled. Lower values cut the overdraw from dense forests. Read each frame in OnIdle, so no
		// change handler is required. A very large value = unlimited (render everything).
		s_viewDistanceVar = ConsoleVarMgr::RegisterConsoleVar("ViewDistance", "Maximum render distance for environment objects like trees (world units). Lower values improve performance in dense forests.", "600");

		s_terrainLodEnabledVar = ConsoleVarMgr::RegisterConsoleVar("TerrainLodEnabled", "Enable or disable terrain level of detail", "1");
		m_cvarChangedSignals += s_terrainLodEnabledVar->Changed.connect(this, &WorldState::OnTerrainLodEnabledChanged);

		s_terrainOcclusionCullingVar = ConsoleVarMgr::RegisterConsoleVar("TerrainOcclusionCulling", "Enable or disable GPU occlusion culling for terrain tiles", "1");
		m_cvarChangedSignals += s_terrainOcclusionCullingVar->Changed.connect(this, &WorldState::OnTerrainOcclusionCullingChanged);

		s_foliageEnabledVar = ConsoleVarMgr::RegisterConsoleVar("FoliageEnabled", "Enable or disable foliage rendering (grass, plants, etc.)", "1");
		m_cvarChangedSignals += s_foliageEnabledVar->Changed.connect(this, &WorldState::OnFoliageEnabledChanged);
		s_foliageDensityVar = ConsoleVarMgr::RegisterConsoleVar("FoliageDensity", "Foliage density multiplier (0.1 to 1.0). Lower values improve performance.", "1.0");
		m_cvarChangedSignals += s_foliageDensityVar->Changed.connect(this, &WorldState::OnFoliageDensityChanged);

		Console::RegisterCommand(command_names::s_reload, [this](const std::string &, const std::string &)
								 { ReloadUI(); }, ConsoleCommandCategory::Debug, "Reloads the user interface.");

		Console::RegisterCommand(command_names::s_toggleAxis, [this](const std::string &, const std::string &)
								 { ToggleAxisVisibility(); }, ConsoleCommandCategory::Debug, "Toggles visibility of the axis display.");

		Console::RegisterCommand(command_names::s_toggleGrid, [this](const std::string &, const std::string &)
								 { ToggleGridVisibility(); }, ConsoleCommandCategory::Debug, "Toggles visibility of the world grid display.");

		Console::RegisterCommand(command_names::s_toggleWire, [this](const std::string &, const std::string &)
								 { ToggleWireframe(); }, ConsoleCommandCategory::Debug, "Toggles wireframe render mode.");

		Console::RegisterCommand(command_names::s_toggleTerrainLOD, [this](const std::string &, const std::string &)
								 {
									 if (m_worldInstance && m_worldInstance->HasTerrain())
									 {
										 const bool enabled = !m_worldInstance->GetTerrain()->IsLodEnabled();
										 m_worldInstance->GetTerrain()->SetLodEnabled(enabled);
										 if (s_terrainLodEnabledVar)
										 {
											 s_terrainLodEnabledVar->Set(enabled);
										 }
										 ILOG("Terrain LOD " << (enabled ? "enabled" : "disabled"));
									 }
								 },
								 ConsoleCommandCategory::Debug, "Toggles terrain LOD.");

		Console::RegisterCommand(command_names::s_toggleTerrainDebug, [this](const std::string &, const std::string &)
								 {
									 if (m_worldInstance && m_worldInstance->HasTerrain())
									 {
										 const bool visible = !m_worldInstance->GetTerrain()->IsDebugLodVisible();
										 m_worldInstance->GetTerrain()->SetDebugLodIsVisible(visible);
										 ILOG("Terrain LOD debug " << (visible ? "enabled" : "disabled"));
									 }
								 },
								 ConsoleCommandCategory::Debug, "Toggles terrain LOD debug visualization.");

		Console::RegisterCommand(command_names::s_freezeCulling, [this](const std::string &, const std::string &)
								 {
				// Ensure that the frustum planes are recalculated to immediately see the effect
				m_playerController->GetCamera().InvalidateView();

				// Toggle culling freeze and log current culling state
				m_scene->FreezeRendering(!m_scene->IsRenderingFrozen());
				ILOG(m_scene->IsRenderingFrozen() ? "Culling is now frozen" : "Culling is no longer frozen"); }, ConsoleCommandCategory::Debug, "Toggles culling.");

		OnShadowTextureSizeChanged(*s_shadowTextureSizeVar, "");
		OnShadowQualityChanged(*s_shadowQualityVar, "");
		OnShadowTemporalChanged(*s_shadowTemporalVar, "");
		OnDepthPrepassChanged(*s_depthPrepassVar, "");
		OnRenderShadowsChanged(*s_renderShadowsVar, "");
		OnShadowBiasChanged(*s_depthBiasVar, "");
		OnFoliageEnabledChanged(*s_foliageEnabledVar, "");
		OnFoliageDensityChanged(*s_foliageDensityVar, "");
	}

	void WorldState::RemoveGameplayCommands()
	{
		ConsoleVarMgr::UnregisterConsoleVar("DebugTargetPath");
		ConsoleVarMgr::UnregisterConsoleVar("RenderShadows");
		ConsoleVarMgr::UnregisterConsoleVar("ShadowDepthBias");
		ConsoleVarMgr::UnregisterConsoleVar("ShadowSlopeBias");
		ConsoleVarMgr::UnregisterConsoleVar("ShadowClampBias");
		ConsoleVarMgr::UnregisterConsoleVar("ShadowTextureSize");
		ConsoleVarMgr::UnregisterConsoleVar("ShadowQuality");
		ConsoleVarMgr::UnregisterConsoleVar("ShadowTemporal");
		ConsoleVarMgr::UnregisterConsoleVar("gxRenderScale");
		ConsoleVarMgr::UnregisterConsoleVar("gxDepthPrepass");
		ConsoleVarMgr::UnregisterConsoleVar("ViewDistance");
		ConsoleVarMgr::UnregisterConsoleVar("FoliageEnabled");
		ConsoleVarMgr::UnregisterConsoleVar("FoliageDensity");

		m_cvarChangedSignals.disconnect();

		const String commandsToRemove[] = {
			command_names::s_toggleAxis,
			command_names::s_toggleGrid,
			command_names::s_toggleWire,
			command_names::s_toggleTerrainLOD,
			command_names::s_toggleTerrainDebug,
			command_names::s_freezeCulling,
			command_names::s_reload};

		for (const auto &command : commandsToRemove)
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
		if (m_worldInstance->GetTerrain())
		{
			m_worldInstance->GetTerrain()->SetWireframeVisible(!m_worldInstance->GetTerrain()->IsWireframeVisible());
			if (m_worldInstance->GetTerrain()->IsWireframeVisible())
			{
				ILOG("Wireframe active");
			}
			else
			{
				ILOG("Wireframe inactive");
			}
		}
	}

	void WorldState::WaitForWorldLoading()
	{
		ASSERT(m_playerController);
		const auto position = m_playerController->GetControlledUnit()->GetPosition();

		// Ensure terrain is properly reloaded
		PagePosition worldSize(64, 64);
		const auto pagePos = PagePosition(static_cast<uint32>(floor(position.x / terrain::constants::PageSize)) + 32, static_cast<uint32>(floor(position.z / terrain::constants::PageSize)) + 32);
		ForEachPageInSquare(
			worldSize, pagePos, 1, [this](const PagePosition &page)
			{
				terrain::Page* terrainPage = m_worldInstance->GetTerrain()->GetPage(page.x(), page.y());
				if (terrainPage)
				{
					if (terrainPage->Prepare())
					{
						while (!terrainPage->Load());
					}
				}

				m_worldInstance->LoadPageEntities(page.x(), page.y()); });

		size_t dispatched = 0;
		while (m_dispatcher.poll_one())
		{
			dispatched++;
		}

		// Poll one time
		size_t jobsDone = 0;
		while (m_workQueue.poll_one())
		{
			jobsDone++;
		}

		m_foliage->RebuildAll();

		m_dispatcher.post([this, jobsDone, dispatched]()
			{
				m_worldLoaded = true;
				ILOG("World loading finished. Dispatched: " << dispatched << ", Jobs done: " << jobsDone);

				// Hide loading screen
				LoadingScreen::Hide(); 
			});
	}

	PacketParseResult WorldState::OnUpdateObject(game::IncomingPacket &packet)
	{
		return HandleObjectUpdate(packet);
	}

	PacketParseResult WorldState::HandleObjectUpdate(io::Reader &packet)
	{
		uint16 numObjectUpdates;
		if (!(packet >> io::read<uint16>(numObjectUpdates)))
		{
			ELOG("Failed to read update object count!");
			return PacketParseResult::Disconnect;
		}

		auto result = PacketParseResult::Disconnect;
		bool hasItemUpdates = false;
		for (auto i = 0; i < numObjectUpdates; ++i)
		{
			result = PacketParseResult::Disconnect;

			bool creation = false;
			ObjectTypeId typeId;
			if (!(packet >> io::read<uint8>(typeId) >> io::read<uint8>(creation)))
			{
				ELOG("Failed to read object update type");
				return PacketParseResult::Disconnect;
			}

			if (creation)
			{
				// Create game object from deserialization
				std::shared_ptr<GameObjectC> object;

				switch (typeId)
				{
				case ObjectTypeId::Unit:
					object = std::make_shared<GameUnitC>(*m_scene, *this, m_project, g_mapId);
					break;
				case ObjectTypeId::Player:
					object = std::make_shared<GamePlayerC>(*m_scene, *this, m_project, g_mapId, &m_audio);
					break;
				case ObjectTypeId::Item:
					object = std::make_shared<GameItemC>(*m_scene, *this, m_project);
					break;
				case ObjectTypeId::Container:
					object = std::make_shared<GameBagC>(*m_scene, *this, m_project);
					break;
				case ObjectTypeId::Object:
					object = std::make_shared<GameWorldObjectC>(*m_scene, m_project, *this, g_mapId);
					break;
				default:
					ASSERT(!!"Unknown object type");
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
					m_partyInfo.OnPlayerSpawned(static_cast<GamePlayerC &>(*object));
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

					m_playerObservers += object->RegisterMirrorHandler(object_fields::Guild, 2, *this, &WorldState::OnPlayerGuildChanged);

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
							} });

					m_playerController->SetControlledUnit(std::dynamic_pointer_cast<GameUnitC>(object));
					FrameManager::Get().TriggerLuaEvent("PLAYER_ENTER_WORLD");

					if (m_playerController->GetControlledUnit()->GetHealth() <= 0)
					{
						FrameManager::Get().TriggerLuaEvent("PLAYER_DEAD");
					}

					m_questClient.UpdateQuestLog(*std::static_pointer_cast<GamePlayerC>(object));

					OnCombatModeChanged(object->GetGuid());

					// Trigger guild change notification
					if (object->Get<uint64>(object_fields::Guild) != 0)
					{
						OnPlayerGuildChanged(object->GetGuid());
					}

					ILOG("Received spawn packet for player controlled unit");

					// Ensure world is loaded entirely
					WaitForWorldLoading();

					// Update talent trees
					m_talentClient.NotifyCharacterClassChanged();
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

				// Check if this was an item update for the active player
				if ((obj->GetTypeId() == ObjectTypeId::Item || obj->GetTypeId() == ObjectTypeId::Container) &&
					obj->Get<uint64>(object_fields::ItemOwner) == ObjectMgr::GetActivePlayerGuid())
				{
					hasItemUpdates = true;
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

		// If we had item updates for the active player, trigger UI refresh
		if (hasItemUpdates)
		{
			FrameManager::Get().TriggerLuaEvent("ITEM_COUNT_CHANGED");
		}

		return result;
	}

	PacketParseResult WorldState::OnCompressedUpdateObject(game::IncomingPacket &packet)
	{
		// Body layout: uint32 uncompressed size followed by the zlib-compressed UpdateObject body.
		uint32 uncompressedSize;
		if (!(packet >> io::read<uint32>(uncompressedSize)))
		{
			ELOG("Failed to read uncompressed size of compressed update object packet!");
			return PacketParseResult::Disconnect;
		}

		if (uncompressedSize == 0)
		{
			ELOG("Compressed update object packet declares an empty payload!");
			return PacketParseResult::Disconnect;
		}

		// The remaining bytes of the packet body are the compressed payload.
		const uint32 compressedSize = packet.GetSize() - sizeof(uint32);
		if (compressedSize == 0)
		{
			ELOG("Compressed update object packet has no payload!");
			return PacketParseResult::Disconnect;
		}

		std::vector<char> compressed(compressedSize);
		if (packet.getSource()->read(compressed.data(), compressedSize) != compressedSize)
		{
			ELOG("Failed to read compressed update object payload!");
			return PacketParseResult::Disconnect;
		}

		// Decompress into a buffer of the declared uncompressed size.
		std::vector<char> decompressed(uncompressedSize);
		uLongf destLen = uncompressedSize;
		const int result = uncompress(reinterpret_cast<Bytef*>(decompressed.data()), &destLen,
			reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(compressedSize));
		if (result != Z_OK || destLen != uncompressedSize)
		{
			ELOG("Failed to decompress update object packet (zlib error " << result << ")");
			return PacketParseResult::Disconnect;
		}

		// Parse the decompressed update body exactly like an uncompressed UpdateObject packet.
		io::MemorySource decompressedSource(decompressed.data(), decompressed.data() + decompressed.size());
		io::Reader reader(decompressedSource);
		return HandleObjectUpdate(reader);
	}

	PacketParseResult WorldState::OnDestroyObjects(game::IncomingPacket &packet)
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

			// Check if this is an item owned by the active player before removing it
			bool shouldTriggerItemEvent = false;
			if (const auto object = ObjectMgr::Get<GameObjectC>(id))
			{
				if ((object->GetTypeId() == ObjectTypeId::Item || object->GetTypeId() == ObjectTypeId::Container) &&
					object->Get<uint64>(object_fields::ItemOwner) == ObjectMgr::GetActivePlayerGuid())
				{
					shouldTriggerItemEvent = true;
				}
			}

			ObjectMgr::RemoveObject(id);

			// Trigger UI event after the object has been removed from ObjectMgr
			if (shouldTriggerItemEvent)
			{
				FrameManager::Get().TriggerLuaEvent("ITEM_COUNT_CHANGED");
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMovement(game::IncomingPacket &packet)
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

		if (ObjectMgr::GetActivePlayerGuid() != characterGuid)
		{
			// Route all movement to the dead-reckoning renderer.
			// MoveSetFacing and MoveStopTurn are treated as facing-only updates
			// (no position extrapolation reset) — the renderer handles this distinction.
			const uint16 opcode = packet.GetId();
			const bool facingOnly =
			    opcode == game::realm_client_packet::MoveSetFacing;

			if (facingOnly)
			{
				unitPtr->ApplyRemoteFacing(movementInfo.facing);
			}
			else
			{
				unitPtr->EnqueueRemoteMovement(movementInfo);
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnChatMessage(game::IncomingPacket &packet)
	{
		uint64 characterGuid;
		ChatType type;
		String message;
		uint8 flags;
		if (!(packet >> io::read_packed_guid(characterGuid) >> io::read<uint8>(type) >> io::read_limited_string<512>(message) >> io::read<uint8>(flags)))
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
			case ChatType::UnitSay:
				chatMessageType = "UNIT_SAY";
				break;
			case ChatType::UnitYell:
				chatMessageType = "UNIT_YELL";
				break;
			case ChatType::UnitEmote:
				chatMessageType = "UNIT_EMOTE";
				break;
			default:
				UNREACHABLE();
				break;
			}

			FrameManager::Get().TriggerLuaEvent("CHAT_MSG_" + chatMessageType, speaker, message);
		}
		else
		{
			// System messages (GUID 0) do not require a name lookup
			if (type == ChatType::System)
			{
				FrameManager::Get().TriggerLuaEvent("CHAT_MSG_SYSTEM", message);
				return PacketParseResult::Pass;
			}

			m_cache.GetNameCache().Get(characterGuid, [this, type, message, flags](uint64, const String &name)
									   {
					String chatMessageType = "SAY";
					switch (type)
					{
					case ChatType::Channel: chatMessageType = "CHANNEL"; break;
					case ChatType::Yell: chatMessageType = "YELL"; break;
					case ChatType::Emote: chatMessageType = "EMOTE"; break;
					case ChatType::Group: chatMessageType = "PARTY"; break;
					case ChatType::Guild: chatMessageType = "GUILD"; break;
					case ChatType::Whisper: chatMessageType = "WHISPER"; break;
					case ChatType::Raid: chatMessageType = "RAID"; break;
					}

					FrameManager::Get().TriggerLuaEvent("CHAT_MSG_" + chatMessageType, name, message); });
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnNameQueryResult(game::IncomingPacket &packet)
	{
		uint64 guid;
		bool succeeded;
		String name;
		if (!(packet >> io::read_packed_guid(guid) >> io::read<uint8>(succeeded)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!succeeded)
		{
			ELOG("Unable to retrieve unit name for unit " << log_hex_digit(guid));
			return PacketParseResult::Pass;
		}

		if (!(packet >> io::read_string(name)))
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

	PacketParseResult WorldState::OnCreatureQueryResult(game::IncomingPacket &packet)
	{
		uint64 id;
		bool succeeded;
		if (!(packet >> io::read_packed_guid(id) >> io::read<uint8>(succeeded)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!succeeded)
		{
			ELOG("Creature query for id " << log_hex_digit(id) << " failed");
			return PacketParseResult::Pass;
		}

		CreatureInfo entry{id};
		if (!(packet >> io::read_string(entry.name) >> io::read_string(entry.subname)))
		{
			ELOG("Creature query for id " << log_hex_digit(id) << " failed");
			return PacketParseResult::Pass;
		}

		m_cache.GetCreatureCache().NotifyObjectResponse(id, entry);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnItemQueryResult(game::IncomingPacket &packet)
	{
		uint64 id;
		bool succeeded;
		if (!(packet >> io::read_packed_guid(id) >> io::read<uint8>(succeeded)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!succeeded)
		{
			ELOG("Item query for id " << log_hex_digit(id) << " failed");
			return PacketParseResult::Pass;
		}

		ItemInfo entry{id};
		if (!(packet >> entry))
		{
			ELOG("Failed to read item info!");
			return PacketParseResult::Disconnect;
		}



		m_cache.GetItemCache().NotifyObjectResponse(id, entry);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnObjectQueryResult(game::IncomingPacket &packet)
	{
		uint64 id;
		bool succeeded;
		if (!(packet >> io::read_packed_guid(id) >> io::read<uint8>(succeeded)))
		{
			return PacketParseResult::Disconnect;
		}

		if (!succeeded)
		{
			ELOG("Object query for id " << log_hex_digit(id) << " failed");
			return PacketParseResult::Pass;
		}

		ObjectInfo entry{id};
		if (!(packet >> entry))
		{
			ELOG("Failed to read object info!");
			return PacketParseResult::Disconnect;
		}

		m_cache.GetObjectCache().NotifyObjectResponse(id, entry);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnInitialSpells(game::IncomingPacket &packet)
	{
		std::vector<uint32> spellIds;
		if (!(packet >> io::read_container<uint16>(spellIds)))
		{
			return PacketParseResult::Disconnect;
		}

		std::vector<const proto_client::SpellEntry *> spells;
		spells.reserve(spellIds.size());

		for (const auto &spellId : spellIds)
		{
			const auto *spell = m_project.spells.getById(spellId);
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
		m_talentClient.NotifyCharacterClassChanged();

		FrameManager::Get().TriggerLuaEvent("PLAYER_SPELLS_CHANGED");

		return PacketParseResult::Pass;
	}

	// UnpackMovementVector was removed — intermediate waypoints are now transmitted as
	// raw floats (3 × float) instead of the old lossy 11/10-bit packed uint32 format.
	// The packed format silently overflowed for routes longer than ~256 world-units
	// from the (start+end)/2 midpoint, corrupting every waypoint on long NPC routes.

	PacketParseResult WorldState::OnCreatureMove(game::IncomingPacket &packet)
	{
		std::vector<Vector3> path;

		uint64 guid;
		Vector3 startPosition;
		Vector3 endPosition;
		GameTime startTime;
		GameTime endTime;
		uint32 pathSize;
		std::optional<Radian> targetRotation;
		UnitMovementMode movementMode = unit_movement_mode::Run;

		if (!(packet >> io::read_packed_guid(guid) >> io::read<float>(startPosition.x) >> io::read<float>(startPosition.y) >> io::read<float>(startPosition.z) >> io::read<GameTime>(startTime) >> io::read<GameTime>(endTime) >> io::read<uint32>(pathSize) >> io::read<float>(endPosition.x) >> io::read<float>(endPosition.y) >> io::read<float>(endPosition.z)))
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

		uint8 rawMovementMode = unit_movement_mode::Run;
		if (!(packet >> io::read<uint8>(rawMovementMode)))
		{
			return PacketParseResult::Disconnect;
		}

		if (rawMovementMode < unit_movement_mode::Count_)
		{
			movementMode = static_cast<UnitMovementMode>(rawMovementMode);
		}

		// Find unit by guid
		const auto unitPtr = ObjectMgr::Get<GameUnitC>(guid);
		if (!unitPtr)
		{
			// We don't know the unit
			WLOG("Received movement packet for unknown unit id " << log_hex_digit(guid));
			return PacketParseResult::Pass;
		}

		path.push_back(startPosition);

		const bool isPlayer = unitPtr->GetTypeId() == ObjectTypeId::Player;
		(void)isPlayer;

		// Read intermediate waypoints written as raw floats.
		// pathSize = total waypoints not counting startPosition; endPosition is #1.
		// So there are (pathSize - 1) intermediate points to read.
		// Previously this used a lossy 11/10-bit packed format relative to the
		// route midpoint, which overflowed for long multi-waypoint NPC routes.
		if (pathSize > 1)
		{
			for (uint32 i = 1; i < pathSize; ++i)  // pathSize-1 iterations (was pathSize-2, off by one)
			{
				Vector3 p;
				if (!(packet >> io::read<float>(p.x) >> io::read<float>(p.y) >> io::read<float>(p.z)))
				{
					return PacketParseResult::Disconnect;
				}

				path.push_back(p);
			}
		}

		path.push_back(endPosition);

		// Check whether we should update the output debug path
		if (guid == ObjectMgr::GetSelectedObjectGuid())
		{
			if (m_debugPathVisualizer)
			{
				if (s_debugOutputPathVar && s_debugOutputPathVar->GetBoolValue())
				{
					m_debugPathVisualizer->ShowPath(path);
				}
			}
		}

		unitPtr->SetMovementPath(path, endTime - startTime, targetRotation, movementMode);

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellLearnedOrUnlearned(game::IncomingPacket &packet)
	{
		uint32 spellId;
		if (!(packet >> io::read<uint32>(spellId)))
		{
			return PacketParseResult::Disconnect;
		}

		const auto *spell = m_project.spells.getById(spellId);
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

					const auto *spellAtSlot = m_actionBar.GetActionButtonSpell(i);
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
					m_actionBar.SetActionButton(emptySlot, {static_cast<uint16>(spellId), action_button_type::Spell});
				}
			}

			// Update possible trainer spell list
			m_trainerClient.OnSpellLearned(spellId);

			// Update talent information
			m_talentClient.OnSpellLearned(spellId);

			FrameManager::Get().TriggerLuaEvent("SPELL_LEARNED", spell->name());
			FrameManager::Get().TriggerLuaEvent("PLAYER_TALENT_UPDATE");
		}
		else
		{
			m_playerController->GetControlledUnit()->UnlearnSpell(spellId);

			// Update talent information for unlearned spell
			m_talentClient.OnSpellUnlearned(spellId);

			FrameManager::Get().TriggerLuaEvent("PLAYER_TALENT_UPDATE");
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellStart(game::IncomingPacket &packet)
	{
		uint64 casterId;
		uint32 spellId;
		GameTime castTime;
		SpellTargetMap targetMap;
		uint32 cooldownMs = 0;

		if (!(packet >> io::read_packed_guid(casterId) >> io::read<uint32>(spellId) >> io::read<GameTime>(castTime) >> targetMap >> io::read<uint32>(cooldownMs)))
		{
			return PacketParseResult::Disconnect;
		}

		const auto *spell = m_project.spells.getById(spellId);
		ASSERT(spell);

		if (const std::shared_ptr<GameUnitC> casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
		{
			if (castTime > 0)
			{
				// Trigger spell visualization for casting start and looped casting animation
				std::vector<GameUnitC *> targets;
				SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::StartCast, *spell, casterUnit.get(), targets);
				SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::Casting, *spell, casterUnit.get(), targets);
			}
		}

		if (m_playerController->GetControlledUnit())
		{
			if (casterId == m_playerController->GetControlledUnit()->GetGuid() && castTime > 0)
			{
				m_spellCast.OnSpellStart(*spell, castTime);

				if (cooldownMs > 0)
				{
					m_cooldownManager.StartCooldown(spellId, cooldownMs);
				}
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellGo(game::IncomingPacket &packet)
	{
		uint64 casterId;
		uint32 spellId;
		GameTime gameTime;
		SpellTargetMap targetMap;
		uint32 cooldownMs = 0;

		if (!(packet >> io::read_packed_guid(casterId) >> io::read<uint32>(spellId) >> io::read<GameTime>(gameTime) >> targetMap >> io::read<uint32>(cooldownMs)))
		{
			return PacketParseResult::Disconnect;
		}

		// Get the spell
		const auto *spell = m_project.spells.getById(spellId);
		ASSERT(spell);

		// Get spell visualization for projectile config
		const proto_client::SpellVisualization *visualization = nullptr;
		if (spell->has_visualization_id())
		{
			visualization = m_project.spellVisualizations.getById(spell->visualization_id());
		}

		// Handle projectile vs instant spells
		if (spell->speed() > 0.0f)
		{
			// Projectile spell: spawn projectile if we have a unit target
			if (targetMap.HasUnitTarget())
			{
				auto casterUnit = ObjectMgr::Get<GameUnitC>(casterId);
				const uint64 unitTargetGuid = targetMap.GetUnitTarget();
				auto targetUnit = ObjectMgr::Get<GameUnitC>(unitTargetGuid);

				if (casterUnit && targetUnit && m_projectileManager)
				{
					// Check if there's a CastSucceeded animation - if so, defer projectile spawn
					bool hasCastSucceededAnimation = false;
					if (visualization)
					{
						const uint32 eventKey = static_cast<uint32>(proto_client::CAST_SUCCEEDED);
						const auto& eventMap = visualization->kits_by_event();
						auto kitIt = eventMap.find(eventKey);
						if (kitIt != eventMap.end())
						{
							for (const auto& kit : kitIt->second.kits())
							{
								if (kit.has_animation_name() && !kit.animation_name().empty())
								{
									hasCastSucceededAnimation = true;
									break;
								}
							}
						}
					}

					if (hasCastSucceededAnimation)
					{
						// Create pending projectile and wait for SpellGo animation notify or animation end
						auto pending = std::make_unique<PendingProjectile>();
						pending->spellId = spellId;
						pending->casterGuid = casterId;
						pending->targetGuid = unitTargetGuid;
						pending->visualization = visualization;
						pending->hasCastSucceededAnimation = true;
						pending->creationTime = GetAsyncTimeMs();
						
						// Capture raw pointer for use in lambdas before moving
						PendingProjectile* pendingPtr = pending.get();

						// Subscribe to animation notify signals on the caster
						pending->connections += casterUnit->animationNotifyTriggered.connect(
							[this, pendingPtr](GameUnitC& unit, const AnimationNotify& notify, const String&, const AnimationState&)
							{
								if (notify.GetType() == AnimationNotifyType::SpellGo)
								{
									// SpellGo notify triggered - spawn the projectile now
									SpawnPendingProjectile(pendingPtr);
								}
							});

						// Also check if animation ends without SpellGo notify (fallback)
						// We need to check on Update if the one-shot animation has ended
						// For simplicity, we'll set a timeout fallback

						m_pendingProjectiles.push_back(std::move(pending));
					}
					else
					{
						// No animation, spawn immediately
						m_projectileManager->SpawnProjectile(*spell, visualization, casterUnit.get(), targetUnit.get());
					}
				}
			}
		}
		else
		{
			// Instant spells: directly dispatch impact to unit targets if any
			if (targetMap.HasUnitTarget())
			{
				std::vector<GameUnitC *> targets;
				if (auto targetUnit = ObjectMgr::Get<GameUnitC>(targetMap.GetUnitTarget()))
				{
					targets.push_back(targetUnit.get());
				}
				SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::Impact, *spell, nullptr, targets);
			}
		}
		if (const std::shared_ptr<GameUnitC> casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
		{
			// Trigger spell visualization for cast succeeded
			std::vector<GameUnitC *> targets;
			SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::CastSucceeded, *spell, casterUnit.get(), targets);
		}

		if (casterId == ObjectMgr::GetActivePlayerGuid())
		{
			m_spellCast.OnSpellGo(spellId);

			// Start cooldown if there is one
			if (cooldownMs > 0)
			{
				m_cooldownManager.StartCooldown(spellId, cooldownMs);
			}
		}

		return PacketParseResult::Pass;
	}

	void WorldState::SpawnPendingProjectile(PendingProjectile* pending)
	{
		if (!pending || !m_projectileManager)
		{
			return;
		}

		// Get spell
		const auto* spell = m_project.spells.getById(pending->spellId);
		if (!spell)
		{
			return;
		}

		// Get caster and target units
		auto casterUnit = ObjectMgr::Get<GameUnitC>(pending->casterGuid);
		auto targetUnit = ObjectMgr::Get<GameUnitC>(pending->targetGuid);

		if (casterUnit && targetUnit)
		{
			// Calculate how long the projectile was delayed by the animation
			float animationDelay = 0.0f;
			if (pending->creationTime > 0)
			{
				const GameTime now = GetAsyncTimeMs();
				if (now > pending->creationTime)
				{
					animationDelay = static_cast<float>(now - pending->creationTime) / 1000.0f;
				}
			}

			m_projectileManager->SpawnProjectile(*spell, pending->visualization, casterUnit.get(), targetUnit.get(), animationDelay);
		}

		// Remove this pending projectile from the list
		auto it = std::find_if(m_pendingProjectiles.begin(), m_pendingProjectiles.end(),
			[pending](const std::unique_ptr<PendingProjectile>& p)
			{
				return p.get() == pending;
			});

		if (it != m_pendingProjectiles.end())
		{
			m_pendingProjectiles.erase(it);
		}
	}

	void WorldState::UpdatePendingProjectiles()
	{
		// Check pending projectiles for animation end fallback
		for (auto it = m_pendingProjectiles.begin(); it != m_pendingProjectiles.end(); )
		{
			PendingProjectile* pending = it->get();
			
			// Get caster unit to check animation state
			auto casterUnit = ObjectMgr::Get<GameUnitC>(pending->casterGuid);
			if (!casterUnit)
			{
				// Caster no longer exists, just remove the pending projectile
				it = m_pendingProjectiles.erase(it);
				continue;
			}

			// Check if one-shot animation has ended
			// If the unit has no active one-shot animation and we were waiting for SpellGo,
			// spawn the projectile as fallback
			if (pending->hasCastSucceededAnimation && !casterUnit->IsPlayingOneShotAnimation())
			{
				// Animation ended without SpellGo notify - spawn as fallback
				SpawnPendingProjectile(pending);
				// SpawnPendingProjectile removes from the list, so just break to avoid invalidating iterator
				// Actually, we need to restart iteration since the list was modified
				it = m_pendingProjectiles.begin();
				continue;
			}

			++it;
		}
	}

	PacketParseResult WorldState::OnSpellFailure(game::IncomingPacket &packet)
	{
		static const char *s_spellCastResultStrings[] = {
			"SPELL_CAST_FAILED_AFFECTING_COMBAT",
			"SPELL_CAST_FAILED_ALREADY_AT_FULL_HEALTH",
			"SPELL_CAST_FAILED_ALREADY_AT_FULL_POWER",
			"SPELL_CAST_FAILED_BAD_TARGETS",
			"SPELL_CAST_FAILED_CASTER_DEAD",
			"SPELL_CAST_FAILED_ERROR",
			"SPELL_CAST_FAILED_INTERRUPTED",
			"SPELL_CAST_FAILED_LEVEL_REQUIREMENT",
			"SPELL_CAST_FAILED_MOVING",
			"SPELL_CAST_FAILED_NOT_KNOWN",
			"SPELL_CAST_FAILED_NOT_READY",
			"SPELL_CAST_FAILED_NO_POWER",
			"SPELL_CAST_FAILED_ONLY_INDOORS",
			"SPELL_CAST_FAILED_ONLY_STEALTHED",
			"SPELL_CAST_FAILED_OUT_OF_RANGE",
			"SPELL_CAST_FAILED_REAGENTS",
			"SPELL_CAST_FAILED_SPELL_IN_PROGRESS",
			"SPELL_CAST_FAILED_TARGET_NOT_DEAD",
			"SPELL_CAST_FAILED_UNIT_NOT_BEHIND",
			"SPELL_CAST_FAILED_UNIT_NOT_INFRONT",
			"SPELL_CAST_FAILED_LINE_OF_SIGHT"};

		static const char *s_unknown = "UNKNOWN";

		uint64 casterId;
		uint32 spellId;
		GameTime gameTime;
		uint8 result;

		if (!(packet >> io::read_packed_guid(casterId) >> io::read<uint32>(spellId) >> io::read<GameTime>(gameTime) >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		const auto *spell = m_project.spells.getById(spellId);
		if (spell)
		{
			if (const std::shared_ptr<GameUnitC> casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
			{
				// Trigger spell visualization for cancel cast
				std::vector<GameUnitC *> targets;
				SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::CancelCast, *spell, casterUnit.get(), targets);
			}
		}

		if (casterId == ObjectMgr::GetActivePlayerGuid())
		{
			const bool wasCastingThisSpell = m_spellCast.GetCastingSpellId() == spellId;
			const bool castWasConfirmedByServer = m_spellCast.HasServerConfirmedCastStart(spellId);
			m_spellCast.OnSpellFailure(spellId);
			if (wasCastingThisSpell &&
				castWasConfirmedByServer &&
				spell &&
				(spell->cooldownflags() & spell_cooldown_flags::StartOnCastStart) != 0)
			{
				m_cooldownManager.ClearCooldown(spellId);
			}

			const char *errorMessage = s_unknown;
			if (result < std::size(s_spellCastResultStrings))
			{
				errorMessage = s_spellCastResultStrings[result];
			}
			FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FINISH", false);
			FrameManager::Get().TriggerLuaEvent("PLAYER_SPELL_CAST_FAILED", errorMessage);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnChannelStart(game::IncomingPacket &packet)
	{
		uint64 casterId;
		uint32 spellId;
		int32 duration;

		if (!(packet >> io::read_packed_guid(casterId) >> io::read<uint32>(spellId) >> io::read<int32>(duration)))
		{
			return PacketParseResult::Disconnect;
		}

		const auto *spell = m_project.spells.getById(spellId);
		if (!spell)
		{
			return PacketParseResult::Pass;
		}

		if (const std::shared_ptr<GameUnitC> casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
		{
			if (duration > 0)
			{
				std::vector<GameUnitC *> targets;
				SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::StartCast, *spell, casterUnit.get(), targets);
				SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::Casting, *spell, casterUnit.get(), targets);
			}
		}

		if (m_playerController->GetControlledUnit())
		{
			if (casterId == m_playerController->GetControlledUnit()->GetGuid() && duration > 0)
			{
				m_spellCast.OnChannelStart(*spell, static_cast<GameTime>(duration));
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnChannelUpdate(game::IncomingPacket &packet)
	{
		uint64 casterId;
		GameTime timeLeft;

		if (!(packet >> io::read_packed_guid(casterId) >> io::read<GameTime>(timeLeft)))
		{
			return PacketParseResult::Disconnect;
		}

		if (m_playerController->GetControlledUnit())
		{
			if (casterId == m_playerController->GetControlledUnit()->GetGuid())
			{
				m_spellCast.OnChannelUpdate(casterId, timeLeft);
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnAttackStart(game::IncomingPacket &packet)
	{
		uint64 attackerGuid, victimGuid;
		GameTime attackTime;
		if (!(packet >> io::read_packed_guid(attackerGuid) >> io::read_packed_guid(victimGuid) >> io::read<GameTime>(attackTime)))
		{
			return PacketParseResult::Disconnect;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnAttackStop(game::IncomingPacket &packet)
	{
		uint64 attackerGuid;
		GameTime attackTime;
		if (!(packet >> io::read_packed_guid(attackerGuid) >> io::read<GameTime>(attackTime)))
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

	PacketParseResult WorldState::OnAttackSwingError(game::IncomingPacket &packet)
	{
		AttackSwingEvent attackSwingError;

		if (!(packet >> io::read<uint32>(attackSwingError)))
		{
			return PacketParseResult::Disconnect;
		}

		m_lastAttackSwingEvent = attackSwingError;
		OnAttackSwingErrorTimer();

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnXpLog(game::IncomingPacket &packet)
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

	PacketParseResult WorldState::OnSpellDamageLog(game::IncomingPacket &packet)
	{
		uint64 targetGuid;
		uint32 amount;
		SpellSchool school;
		uint8 flags;
		uint32 spellId;
		uint32 blocked;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read<uint32>(spellId) >> io::read<uint32>(amount) >> io::read<uint8>(school) >> io::read<uint8>(flags) >> io::read<uint32>(blocked)))
		{
			return PacketParseResult::Disconnect;
		}

		String spellName = "Unknown";
		if (const auto *spell = m_project.spells.getById(spellId))
		{
			spellName = spell->name();
			if (spell->rank() > 0)
			{
				spellName += " (Rank " + std::to_string(spell->rank()) + ")";
			}
		}

		String damageSchoolNameString;
		switch (school)
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
			// If the target was immune to this spell's damage school, show a localized "Immune"
			// text instead of a damage number.
			if (flags & damage_flags::Immune)
			{
				AddWorldTextFrame(target->GetPosition(), Localize(FrameManager::Get().GetLocalization(), "COMBAT_IMMUNE"), Color(1.0f, 1.0f, 0.0f, 1.0f), 2.0f);
			}
			else if (blocked > 0 && amount == 0)
			{
				// The block fully absorbed the ability damage.
				AddWorldTextFrame(target->GetPosition(), Localize(FrameManager::Get().GetLocalization(), "COMBAT_BLOCKED"), Color(1.0f, 1.0f, 0.0f, 1.0f), 2.0f);
			}
			else if (blocked > 0)
			{
				// Partial block: show remaining damage and how much was blocked, e.g. "14 (5 blocked)".
				const String blockedText = std::to_string(amount) + " (" + std::to_string(blocked) + " " +
					Localize(FrameManager::Get().GetLocalization(), "COMBAT_BLOCKED_SUFFIX") + ")";
				AddWorldTextFrame(target->GetPosition(), blockedText, Color(1.0f, 1.0f, 0.0f, 1.0f), 2.0f);
			}
			else
			{
				AddWorldTextFrame(target->GetPosition(), std::to_string(amount), Color(1.0f, 1.0f, 0.0f, 1.0f), 2.0f);
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnNonSpellDamageLog(game::IncomingPacket &packet)
	{
		uint64 targetGuid;
		uint32 amount;
		DamageFlags flags;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read<uint32>(amount) >> io::read<uint8>(flags)))
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

	PacketParseResult WorldState::OnAttackerStateUpdate(game::IncomingPacket &packet)
	{
		uint64 attackerGuid, attackedGuid;
		uint32 totalDamage, school, absorbedDamage, resistedDamage, blockedDamage;
		VictimState victimState;
		HitInfo hitInfo;
		if (!(packet >> io::read_packed_guid(attackerGuid) >> io::read_packed_guid(attackedGuid) >> io::read<uint32>(hitInfo) >> io::read<uint32>(victimState) >> io::read<uint32>(totalDamage) >> io::read<uint32>(school) >> io::read<uint32>(absorbedDamage) >> io::read<uint32>(resistedDamage) >> io::read<uint32>(blockedDamage)))
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
				// Check the specific victim states (dodge/parry/block) before the generic Miss flag:
				// the server sets hit_info::Miss for dodges and parries too (as a "no damage landed"
				// marker), so checking Miss first would mask them as "MISSED".
				if ((hitInfo & hit_info::Immune) || victimState == victim_state::IsImmune)
				{
					damageText = Localize(FrameManager::Get().GetLocalization(), "COMBAT_IMMUNE");
				}
				else if (victimState == victim_state::Dodge)
				{
					damageText = "DODGED";
				}
				else if (victimState == victim_state::Parry)
				{
					damageText = "PARRIED";
				}
				else if (victimState == victim_state::Blocks)
				{
					if (totalDamage == 0)
					{
						// The block fully absorbed the hit.
						damageText = Localize(FrameManager::Get().GetLocalization(), "COMBAT_BLOCKED");
					}
					else
					{
						// Partial block: show remaining damage and how much was blocked, e.g. "14 (5 blocked)".
						damageText = std::to_string(totalDamage) + " (" + std::to_string(blockedDamage) + " " +
							Localize(FrameManager::Get().GetLocalization(), "COMBAT_BLOCKED_SUFFIX") + ")";
					}
				}
				else if (hitInfo & hit_info::Miss)
				{
					damageText = "MISSED"; // Localize
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

	PacketParseResult WorldState::OnLogEnvironmentalDamage(game::IncomingPacket &packet)
	{
		uint64 targetGuid;
		uint32 amount;
		uint8 damageType;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read<uint32>(amount) >> io::read<uint8>(damageType)))
		{
			return PacketParseResult::Disconnect;
		}

		// Find the target unit to display floating combat text
		std::shared_ptr<GameObjectC> target = ObjectMgr::Get<GameObjectC>(targetGuid);
		if (target)
		{
			// Environmental damage is displayed in red-orange to distinguish from regular damage
			AddWorldTextFrame(target->GetPosition(), std::to_string(amount), Color(1.0f, 0.5f, 0.0f, 1.0f), 2.0f);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMovementSpeedChanged(game::IncomingPacket &packet)
	{
		static MovementType typesByPacket[] = {
			movement_type::Walk,
			movement_type::Run,
			movement_type::Backwards,
			movement_type::Swim,
			movement_type::SwimBackwards,
			movement_type::Turn,
			movement_type::Flight,
			movement_type::FlightBackwards};

		uint64 guid;
		MovementInfo movementInfo;
		float speed;
		if (!(packet >> io::read_packed_guid(guid) >> movementInfo >> io::read<float>(speed)))
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

		unit->SetSpeed(type, speed);

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnForceMovementSpeedChange(game::IncomingPacket &packet)
	{
		const std::shared_ptr<GameUnitC> &unit = m_playerController->GetControlledUnit();
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

		if (!(packet >> io::read<uint32>(ackId) >> io::read<float>(speed)))
		{
			WLOG("Failed to read force movement speed change packet!");
			return PacketParseResult::Disconnect;
		}

		// Apply speed modifier announce locally and send ack to the server
		unit->SetSpeed(type, speed);
		m_realmConnector.SendMovementSpeedAck(type, ackId, speed, unit->GetMovementInfo());

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMoveTeleport(game::IncomingPacket &packet)
	{
		uint64 guid;
		if (!(packet >> io::read_packed_guid(guid)))
		{
			ELOG("Failed to read teleported mover guid!");
			return PacketParseResult::Disconnect;
		}

		const std::shared_ptr<GameUnitC> &unit = m_playerController->GetControlledUnit();
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

	PacketParseResult WorldState::OnLevelUp(game::IncomingPacket &packet)
	{
		uint32 newLevel;
		int32 healthDiff, manaDiff, staminaDiff, strengthDiff, agilityDiff, intDiff, spiritDiff, talentPoints, attributePoints;
		if (!(packet >> io::read<uint8>(newLevel) >> io::read<int32>(healthDiff) >> io::read<int32>(manaDiff) >> io::read<int32>(staminaDiff) >> io::read<int32>(strengthDiff) >> io::read<int32>(agilityDiff) >> io::read<int32>(intDiff) >> io::read<int32>(spiritDiff) >> io::read<int32>(talentPoints) >> io::read<int32>(attributePoints)))
		{
			ELOG("Failed to read levelup packet!");
			return PacketParseResult::Disconnect;
		}

		FrameManager::Get().TriggerLuaEvent("PLAYER_LEVEL_UP", newLevel, healthDiff, manaDiff, staminaDiff, strengthDiff, agilityDiff, intDiff, spiritDiff, talentPoints, attributePoints);

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnAuraUpdate(game::IncomingPacket &packet)
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

	PacketParseResult WorldState::OnPeriodicAuraLog(game::IncomingPacket &packet)
	{
		uint64 targetGuid;
		uint64 casterGuid;
		uint32 spellId;
		uint32 auraType;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId) >> io::read<uint32>(auraType)))
		{
			ELOG("Failed to read PeriodicAuraLog packet!");
			return PacketParseResult::Disconnect;
		}

		// Trigger AuraTick visualization event
		if (const auto* spell = m_project.spells.getById(spellId))
		{
			GameUnitC* casterUnit = nullptr;
			if (const auto caster = ObjectMgr::Get<GameUnitC>(casterGuid))
			{
				casterUnit = caster.get();
			}

			if (std::shared_ptr<GameUnitC> targetUnit = ObjectMgr::Get<GameUnitC>(targetGuid))
			{
				std::vector<GameUnitC*> targets { targetUnit.get() };
				SpellVisualizationService::Get().Apply(SpellVisualizationService::Event::AuraTick, *spell, casterUnit, targets);
			}
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

	PacketParseResult WorldState::OnActionButtons(game::IncomingPacket &packet)
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

	PacketParseResult WorldState::OnQuestGiverStatus(game::IncomingPacket &packet)
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
		if (questgiverUnit)
		{
			questgiverUnit->SetQuestGiverStatus(status);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellEnergizeLog(game::IncomingPacket &packet)
	{
		uint64 targetGuid, casterGuid;
		uint32 spellId, powerType, power;
		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId) >> io::read<uint32>(powerType) >> io::read<uint32>(power)))
		{
			ELOG("Failed to read SpellEnergizeLog packet!");
			return PacketParseResult::Disconnect;
		}

		// TODO: Combat log event

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnTransferPending(game::IncomingPacket &packet)
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

		LoadingScreen::Show();

		g_mapId = mapId;

		m_worldChangeHandlers.Clear();
		m_worldChangeHandlers += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::NewWorld, *this, &WorldState::OnNewWorld);

		// Remove all objects at once
		m_playerController->SetControlledUnit(nullptr);
		ObjectMgr::RemoveAllObjects();

		// Clear pings — they belong to the old map
		m_activePings.clear();
		m_minimap.UpdatePings(m_activePings);
		if (m_worldPingVisualizer)
		{
			m_worldPingVisualizer->Clear();
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnNewWorld(game::IncomingPacket &packet)
	{
		uint32 mapId;
		Vector3 position;
		float facingRadianVal;
		if (!(packet >> io::read<uint32>(mapId) >> io::read<float>(position.x) >> io::read<float>(position.y) >> io::read<float>(position.z) >> io::read<float>(facingRadianVal)))
		{
			ELOG("Failed to read NewWorld packet!");
			return PacketParseResult::Disconnect;
		}

		ILOG("Transfer to map " << mapId << " completed! Loading map...");
		g_mapId = mapId;

		m_worldInstance->UnloadAllEntities();

		// Load new map
		LoadMap();

		// Acknowledge the new world
		m_realmConnector.SendMoveWorldPortAck();

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnGroupInvite(game::IncomingPacket &packet)
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

	PacketParseResult WorldState::OnGroupDecline(game::IncomingPacket &packet)
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

	PacketParseResult WorldState::OnPartyCommandResult(game::IncomingPacket &packet)
	{
		PartyOperation command;
		PartyResult result;
		String playerName;
		if (!(packet >> io::read<uint8>(command) >> io::read_container<uint8>(playerName) >> io::read<uint8>(result)))
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

	PacketParseResult WorldState::OnPartyPing(game::IncomingPacket &packet)
	{
		uint64 senderGuid = 0;
		uint8 pingType = 0;
		if (!(packet >> io::read_packed_guid(senderGuid) >> io::read<uint8>(pingType)))
		{
			ELOG("Failed to read PartyPing packet header!");
			return PacketParseResult::Disconnect;
		}

		static constexpr float kPingDuration = 5.0f;

		if (pingType == 0)
		{
			// Position ping
			float x = 0.0f, y = 0.0f, z = 0.0f;
			if (!(packet >> io::read<float>(x) >> io::read<float>(y) >> io::read<float>(z)))
			{
				ELOG("Failed to read PartyPing position payload!");
				return PacketParseResult::Disconnect;
			}

			// Update minimap dots (minimap only cares about XZ)
			bool found = false;
			for (auto& ping : m_activePings)
			{
				if (ping.senderGuid == senderGuid)
				{
					ping.position = Vector3(x, y, z);
					ping.remainingTime = kPingDuration;
					found = true;
					break;
				}
			}
			if (!found)
			{
				m_activePings.push_back({ Vector3(x, y, z), kPingDuration, senderGuid });
			}
			m_minimap.UpdatePings(m_activePings);

			// Update 3D world ping
			if (m_worldPingVisualizer)
			{
				m_worldPingVisualizer->AddPositionPing(senderGuid, Vector3(x, y, z));
			}

			FrameManager::Get().TriggerLuaEvent("PARTY_PING", x, y, z);
		}
		else
		{
			// Unit ping
			uint64 targetGuid = 0;
			if (!(packet >> io::read_packed_guid(targetGuid)))
			{
				ELOG("Failed to read PartyPing unit guid!");
				return PacketParseResult::Disconnect;
			}

			// Remove any minimap dot for this sender (unit pings don't show on minimap as a position)
			m_activePings.erase(
				std::remove_if(m_activePings.begin(), m_activePings.end(),
					[senderGuid](const Minimap::PingDot& p) { return p.senderGuid == senderGuid; }),
				m_activePings.end());
			m_minimap.UpdatePings(m_activePings);

			// Update 3D world ping
			if (m_worldPingVisualizer)
			{
				m_worldPingVisualizer->AddUnitPing(senderGuid, targetGuid);
			}

			FrameManager::Get().TriggerLuaEvent("PARTY_PING_UNIT", targetGuid);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnRandomRollResult(game::IncomingPacket &packet)
	{
		uint64 playerGuid;
		int32 min, max, result;
		if (!(packet >> io::read<uint64>(playerGuid) >> io::read<int32>(min) >> io::read<int32>(max) >> io::read<int32>(result)))
		{
			ELOG("Failed to read RandomRollResult packet!");
			return PacketParseResult::Disconnect;
		}

		m_cache.GetNameCache().Get(playerGuid, [result, min, max](uint64 guid, const String &playerName)
								   { FrameManager::Get().TriggerLuaEvent("RANDOM_ROLL_RESULT", playerName.c_str(), min, max, result); });

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellHealLog(game::IncomingPacket &packet)
	{
		uint64 targetGuid, casterGuid;
		uint32 amount;
		bool crit;
		uint32 spellId;

		if (!(packet >> io::read_packed_guid(targetGuid) >> io::read_packed_guid(casterGuid) >> io::read<uint32>(spellId) >> io::read<uint32>(amount) >> io::read<uint8>(crit)))
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

	PacketParseResult WorldState::OnItemPushResult(game::IncomingPacket &packet)
	{
		uint64 characterGuid;
		bool wasLooted, wasCreated;
		uint8 bag, subslot;
		uint32 itemId;
		uint16 amount, totalCount;

		if (!(packet >> io::read<uint64>(characterGuid) >> io::read<uint8>(wasLooted) >> io::read<uint8>(wasCreated) >> io::read<uint8>(bag) >> io::read<uint8>(subslot) >> io::read<uint32>(itemId) >> io::read<uint16>(amount) >> io::read<uint16>(totalCount)))
		{
			ELOG("Failed to read ItemPushResult packet!");
			return PacketParseResult::Disconnect;
		}

		// Ask for the item info
		m_cache.GetItemCache().Get(itemId, [this, characterGuid, wasLooted, wasCreated, bag, subslot, amount, totalCount](uint64, const ItemInfo &itemInfo)
								   { OnItemPushCallback(itemInfo, characterGuid, wasLooted, wasCreated, bag, subslot, amount, totalCount); });

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnLogoutResponse(game::IncomingPacket &packet)
	{
		ILOG("Successfully logged out of the game...");

		// Normal voluntary logout — return to char select without error.
		LoginState::s_returnReason = LoginReturnReason::NormalLogout;
		m_gameStateMgr.SetGameState(LoginState::Name);

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMessageOfTheDay(game::IncomingPacket &packet)
	{
		String motd;
		if (!(packet >> io::read_container<uint16>(motd, 512)))
		{
			ELOG("Failed to read MessageOfTheDay packet!");
			return PacketParseResult::Disconnect;
		}

		FrameManager::Get().TriggerLuaEvent("MOTD", motd.c_str());

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMoveRoot(game::IncomingPacket &packet)
	{
		uint32 ackId;
		bool applied;
		if (!(packet >> io::read<uint32>(ackId) >> io::read<uint8>(applied)))
		{
			ELOG("Failed to read MoveRoot packet!");
			return PacketParseResult::Disconnect;
		}

		auto player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		MovementInfo info = player->GetMovementInfo();
		if (applied)
		{
			info.movementFlags |= movement_flags::Rooted;
			info.movementFlags &= ~movement_flags::Moving;
			m_playerController->StopAllMovement();
		}
		else
		{
			info.movementFlags &= ~movement_flags::Rooted;
		}
		player->ApplyMovementInfo(info);

		m_realmConnector.SendMoveRootAck(ackId, info);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMoveStun(game::IncomingPacket &packet)
	{
		uint32 ackId;
		bool applied;
		if (!(packet >> io::read<uint32>(ackId) >> io::read<uint8>(applied)))
		{
			ELOG("Failed to read MoveStun packet!");
			return PacketParseResult::Disconnect;
		}

		auto player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		MovementInfo info = player->GetMovementInfo();
		if (applied)
		{
			info.movementFlags |= movement_flags::Rooted;
			info.movementFlags &= ~movement_flags::Moving;
			m_playerController->StopAllMovement();
		}
		else
		{
			info.movementFlags &= ~movement_flags::Rooted;
		}
		player->ApplyMovementInfo(info);

		m_realmConnector.SendMoveStunAck(ackId, info);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMoveSleep(game::IncomingPacket &packet)
	{
		uint32 ackId;
		bool applied;
		if (!(packet >> io::read<uint32>(ackId) >> io::read<uint8>(applied)))
		{
			ELOG("Failed to read MoveSleep packet!");
			return PacketParseResult::Disconnect;
		}

		auto player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		MovementInfo info = player->GetMovementInfo();
		if (applied)
		{
			info.movementFlags |= movement_flags::Rooted;
			info.movementFlags &= ~movement_flags::Moving;
			m_playerController->StopAllMovement();
		}
		else
		{
			info.movementFlags &= ~movement_flags::Rooted;
		}
		player->ApplyMovementInfo(info);

		m_realmConnector.SendMoveSleepAck(ackId, info);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMoveFear(game::IncomingPacket &packet)
	{
		uint32 ackId;
		bool applied;
		if (!(packet >> io::read<uint32>(ackId) >> io::read<uint8>(applied)))
		{
			ELOG("Failed to read MoveFear packet!");
			return PacketParseResult::Disconnect;
		}

		auto player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		MovementInfo info = player->GetMovementInfo();
		if (applied)
		{
			info.movementFlags |= movement_flags::Rooted;
			info.movementFlags &= ~movement_flags::Moving;
			m_playerController->StopAllMovement();
		}
		else
		{
			info.movementFlags &= ~movement_flags::Rooted;
		}
		player->ApplyMovementInfo(info);

		m_realmConnector.SendMoveFearAck(ackId, info);
		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnMoveDisorient(game::IncomingPacket &packet)
	{
		uint32 ackId;
		bool applied;
		if (!(packet >> io::read<uint32>(ackId) >> io::read<uint8>(applied)))
		{
			ELOG("Failed to read MoveDisorient packet!");
			return PacketParseResult::Disconnect;
		}

		auto player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		MovementInfo info = player->GetMovementInfo();
		if (applied)
		{
			info.movementFlags |= movement_flags::Rooted;
			info.movementFlags &= ~movement_flags::Moving;
			m_playerController->StopAllMovement();
		}
		else
		{
			info.movementFlags &= ~movement_flags::Rooted;
		}
		player->ApplyMovementInfo(info);

		m_realmConnector.SendMoveDisorientAck(ackId, info);
		return PacketParseResult::Pass;
	}

// Safe parse helpers — use std::from_chars (no exceptions, no locale overhead).
namespace {
	template<typename T>
	bool ParseUInt(const std::string& s, T& out)
	{
		const auto result = std::from_chars(s.data(), s.data() + s.size(), out);
		return result.ec == std::errc{} && result.ptr == s.data() + s.size();
	}

	bool ParseFloat(const std::string& s, float& out)
	{
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
		const auto result = std::from_chars(s.data(), s.data() + s.size(), out);
		return result.ec == std::errc{} && result.ptr == s.data() + s.size();
#else
		// MSVC older than VS 2019 16.4 may lack floating-point from_chars.
		// Fall back to strtof (sets errno, no exceptions).
		char* end = nullptr;
		errno = 0;
		out = std::strtof(s.c_str(), &end);
		return errno == 0 && end == s.c_str() + s.size();
#endif
	}

	std::vector<std::string> ParseCommandArgs(const std::string& args)
	{
		std::istringstream iss(args);
		std::vector<std::string> tokens;
		std::string token;
		while (iss >> token)
		{
			tokens.push_back(std::move(token));
		}
		return tokens;
	}
}

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_LearnSpell(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.size() != 1)
		{
			ELOG("Usage: learnspell <entry>");
			return;
		}

		uint32 entry = 0;
		if (!ParseUInt(tokens[0], entry)) { ELOG("Invalid entry id: " + tokens[0]); return; }

		m_realmConnector.LearnSpell(entry);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_CreateMonster(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.size() != 1)
		{
			ELOG("Usage: createmonster <entry>");
			return;
		}

		uint32 entry = 0;
		if (!ParseUInt(tokens[0], entry)) { ELOG("Invalid entry id: " + tokens[0]); return; }
		m_realmConnector.CreateMonster(entry);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_DestroyMonster(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
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
			if (!ParseUInt(tokens[0], guid)) { ELOG("Invalid guid: " + tokens[0]); return; }
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
	void WorldState::Command_FaceMe(const std::string &cmd, const std::string &args) const
	{
		const uint64 guid = m_playerController->GetControlledUnit()->Get<uint64>(object_fields::TargetUnit);
		if (guid == 0)
		{
			ELOG("No target selected!");
			return;
		}

		m_realmConnector.FaceMe(guid);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_FollowMe(const std::string &cmd, const std::string &args) const
	{
		const uint64 guid = m_playerController->GetControlledUnit()->Get<uint64>(object_fields::TargetUnit);
		if (guid == 0)
		{
			ELOG("No target selected!");
			return;
		}

		m_realmConnector.FollowMe(guid);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_LevelUp(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);

		uint32 level = 1;
		if (!tokens.empty())
		{
			if (!ParseUInt(tokens[0], level)) { ELOG("Invalid level value: " + tokens[0]); return; }
		}

		m_realmConnector.LevelUp(level);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_GiveMoney(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: money <amount>");
			return;
		}

		uint32 money = 0;
		if (!ParseUInt(tokens[0], money)) { ELOG("Invalid money amount: " + tokens[0]); return; }
		m_realmConnector.GiveMoney(money);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_AddItem(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: additem <item_id> [<count>]");
			return;
		}

		uint32 itemId = 0;
		if (!ParseUInt(tokens[0], itemId)) { ELOG("Invalid item id: " + tokens[0]); return; }
		uint32 count = 1;

		if (tokens.size() > 1)
		{
			if (!ParseUInt(tokens[1], count)) { ELOG("Invalid count: " + tokens[1]); return; }
		}

		m_realmConnector.AddItem(itemId, count);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_WorldPort(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: worldport <map_id> [<x>] [<y>] [<z>] [<facing degree>]");
			return;
		}

		Vector3 position = m_playerController->GetControlledUnit()->GetPosition();
		Radian facing = m_playerController->GetControlledUnit()->GetFacing();

		uint32 mapId = 0;
		if (!ParseUInt(tokens[0], mapId)) { ELOG("Invalid map id: " + tokens[0]); return; }

		if (tokens.size() > 1)
		{
			if (!ParseFloat(tokens[1], position.x)) { ELOG("Invalid x position: " + tokens[1]); return; }
		}
		if (tokens.size() > 2)
		{
			if (!ParseFloat(tokens[2], position.y)) { ELOG("Invalid y position: " + tokens[2]); return; }
		}
		if (tokens.size() > 3)
		{
			if (!ParseFloat(tokens[3], position.z)) { ELOG("Invalid z position: " + tokens[3]); return; }
		}
		if (tokens.size() > 4)
		{
			float facingDeg = 0.0f;
			if (!ParseFloat(tokens[4], facingDeg)) { ELOG("Invalid facing degree: " + tokens[4]); return; }
			facing = Degree(facingDeg);
		}

		m_realmConnector.WorldPort(mapId, position, facing);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_Speed(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: speed <speed>");
			return;
		}

		float speed = 0.0f;
		if (!ParseFloat(tokens[0], speed)) { ELOG("Invalid speed value: " + tokens[0]); return; }
		speed = std::min(50.0f, speed);
		if (speed <= 0.0f)
		{
			ELOG("Speed must be greater than 0!");
			return;
		}

		m_realmConnector.SetSpeed(speed);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_Summon(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: summon <playername>");
			return;
		}

		m_realmConnector.SummonPlayer(tokens[0]);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_Port(const std::string &cmd, const std::string &args) const
	{
		const auto tokens = ParseCommandArgs(args);
		if (tokens.empty())
		{
			ELOG("Usage: port <playername>");
			return;
		}

		m_realmConnector.TeleportToPlayer(tokens[0]);
	}
#endif

#ifdef MMO_WITH_DEV_COMMANDS
	void WorldState::Command_CheckLineOfSight(const std::string &cmd, const std::string &args) const
	{
		const auto player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			ELOG("checklos: no active player");
			return;
		}

		const uint64 targetGuid = player->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			ELOG("checklos: no target selected — select a unit first");
			return;
		}

		m_realmConnector.CheckLineOfSight(targetGuid);
	}
#endif

	PacketParseResult WorldState::OnDebugLineOfSightResult(game::IncomingPacket &packet)
	{
		uint8 hasLos = 0;
		Vector3 from, to, hitPoint;

		if (!(packet
			>> io::read<uint8>(hasLos)
			>> io::read<float>(from.x) >> io::read<float>(from.y) >> io::read<float>(from.z)
			>> io::read<float>(to.x)   >> io::read<float>(to.y)   >> io::read<float>(to.z)
			>> io::read<float>(hitPoint.x) >> io::read<float>(hitPoint.y) >> io::read<float>(hitPoint.z)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("LOS result: " << (hasLos ? "CLEAR" : "BLOCKED")
			<< "  from(" << from.x << "," << from.y << "," << from.z << ")"
			<< "  to(" << to.x << "," << to.y << "," << to.z << ")"
			<< "  hit(" << hitPoint.x << "," << hitPoint.y << "," << hitPoint.z << ")");

		if (m_debugPathVisualizer)
		{
			m_debugPathVisualizer->ShowLineOfSight(from, to, hitPoint, hasLos != 0);
		}

		return PacketParseResult::Pass;
	}

	bool WorldState::LoadMap()
	{
		m_worldInstance.reset();

		m_worldLoaded = false;
		m_timeSyncResponseSent = false;

		const proto_client::MapEntry *map = m_project.maps.getById(g_mapId);
		ASSERT(map);

		const std::string assetPath = "Worlds/" + map->directory() + "/" + map->directory();
		DLOG("Loading map " << assetPath << ".hwld...");

		m_worldInstance = std::make_shared<ClientWorldInstance>(*m_scene, *m_worldRootNode, assetPath, m_workQueue, m_dispatcher);

		const std::unique_ptr<std::istream> streamPtr = AssetRegistry::OpenFile(assetPath + ".hwld");
		if (!streamPtr)
		{
			ELOG("Failed to load world file '" << assetPath << "'");
			return false;
		}

		io::StreamSource source{*streamPtr};
		io::Reader reader{source};

		ClientWorldInstanceDeserializer deserializer{*m_worldInstance};
		if (!deserializer.Read(reader))
		{
			ELOG("Failed to read world '" << assetPath << ".hwld'!");
			return false;
		}

		// Minimap!
		m_minimap.NotifyWorldChanged(map->directory());

		// Load area triggers for this map
		m_areaTriggerManager.LoadTriggersForMap(map->id(), m_project.areaTriggers);

		// Apply terrain LOD setting from console variable
		if (m_worldInstance->HasTerrain() && s_terrainLodEnabledVar)
		{
			m_worldInstance->GetTerrain()->SetLodEnabled(s_terrainLodEnabledVar->GetIntValue() != 0);
		}

		// Apply terrain occlusion culling setting from console variable
		if (m_worldInstance->HasTerrain() && s_terrainOcclusionCullingVar)
		{
			m_worldInstance->GetTerrain()->SetOcclusionCullingEnabled(s_terrainOcclusionCullingVar->GetIntValue() != 0);
		}

		return true;
	}

	void WorldState::OnChatNameQueryCallback(uint64 guid, const String &name)
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

		// Terminal errors: stop auto attack and don't re-queue the error timer.
		// OutOfRange/WrongFacing/NotStanding keep looping so auto attack resumes
		// the moment the condition clears without the player having to re-click.
		if (m_lastAttackSwingEvent == attack_swing_event::TargetDead ||
			m_lastAttackSwingEvent == attack_swing_event::CantAttack)
		{
			m_lastAttackSwingEvent = attack_swing_event::Unknown;

			if (m_playerController && m_playerController->GetControlledUnit())
			{
				m_playerController->GetControlledUnit()->StopAttack();
			}

			return;
		}

		EnqueueNextAttackSwingTimer();
	}

	void WorldState::EnqueueNextAttackSwingTimer()
	{
		m_timers.AddEvent([this]()
						  { OnAttackSwingErrorTimer(); }, GetAsyncTimeMs() + 500);
	}

	void WorldState::OnItemPushCallback(const ItemInfo &itemInfo, uint64 characterGuid, bool wasLooted, bool wasCreated, uint8 bag, uint8 subslot, uint16 amount, uint16 totalCount)
	{
		// TODO: UI events for chat display
		if (characterGuid == ObjectMgr::GetActivePlayerGuid())
		{
			if (wasLooted)
			{
				FrameManager::Get().TriggerLuaEvent("LOOT_ITEM_RECEIVED", itemInfo.name.c_str(), itemInfo.id, itemInfo.quality, amount);
				DLOG("You receive loot: " << itemInfo.name << " x" << amount);
			}
			else
			{
				FrameManager::Get().TriggerLuaEvent("ITEM_RECEIVED", itemInfo.name.c_str(), itemInfo.id, itemInfo.quality, amount);
				DLOG("You received item " << itemInfo.name << " x" << amount);
			}
		}
		else
		{
			// The party member may not be in the local visible world (different cell / out of range).
			// Try the in-memory object first; fall back to the name cache for out-of-range members.
			std::shared_ptr<GameUnitC> unit = ObjectMgr::Get<GameUnitC>(characterGuid);
			if (unit)
			{
				const String &memberName = unit->GetName();
				if (wasLooted)
				{
					FrameManager::Get().TriggerLuaEvent("MEMBER_LOOT_ITEM_RECEIVED", memberName.c_str(), itemInfo.name.c_str(), itemInfo.id, itemInfo.quality, amount);
					DLOG(memberName << " looted item " << itemInfo.name << " x" << amount);
				}
				else
				{
					FrameManager::Get().TriggerLuaEvent("MEMBER_ITEM_RECEIVED", memberName.c_str(), itemInfo.name.c_str(), itemInfo.id, itemInfo.quality, amount);
					DLOG(memberName << " received item " << itemInfo.name << " x" << amount);
				}
			}
			else
			{
				// Unit not visible — look up the name asynchronously from the name cache.
				m_cache.GetNameCache().Get(characterGuid,
					[itemInfo, wasLooted, amount](uint64, const String &memberName)
					{
						if (wasLooted)
						{
							FrameManager::Get().TriggerLuaEvent("MEMBER_LOOT_ITEM_RECEIVED", memberName.c_str(), itemInfo.name.c_str(), itemInfo.id, itemInfo.quality, amount);
							DLOG(memberName << " looted item " << itemInfo.name << " x" << amount);
						}
						else
						{
							FrameManager::Get().TriggerLuaEvent("MEMBER_ITEM_RECEIVED", memberName.c_str(), itemInfo.name.c_str(), itemInfo.id, itemInfo.quality, amount);
							DLOG(memberName << " received item " << itemInfo.name << " x" << amount);
						}
					});
			}
		}
	}

	void WorldState::SendAttackStart(const uint64 victim, const GameTime timestamp)
	{
		m_realmConnector.sendSinglePacket([victim, timestamp](game::OutgoingPacket &packet)
										  {
			packet.Start(game::client_realm_packet::AttackSwing);
			packet << io::write_packed_guid(victim) << io::write<GameTime>(timestamp);
			packet.Finish(); });
	}

	void WorldState::SendAttackStop(const GameTime timestamp)
	{
		m_realmConnector.sendSinglePacket([timestamp](game::OutgoingPacket &packet)
										  {
			packet.Start(game::client_realm_packet::AttackStop);
			packet << io::write<GameTime>(timestamp);
			packet.Finish(); });
	}

	void WorldState::AddWorldTextFrame(const Vector3 &position, const String &text, const Color &color, float duration)
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

	void WorldState::OnPageAvailabilityChanged(const PageNeighborhood &page, bool isAvailable)
	{
		const auto &mainPage = page.GetMainPage();
		const PagePosition &pos = mainPage.GetPosition();

		if (isAvailable)
		{
			m_worldInstance->LoadPageEntities(pos.x(), pos.y());
		}
		else
		{
			m_worldInstance->UnloadPageEntities(pos.x(), pos.y());
		}

		if (m_worldInstance->HasTerrain())
		{
			auto *page = m_worldInstance->GetTerrain()->GetPage(pos.x(), pos.y());
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

		const auto &camPos = m_playerController->GetCamera().GetDerivedPosition();
		return PagePosition(static_cast<uint32>(
								floor(camPos.x / terrain::constants::PageSize)) +
								32,
							static_cast<uint32>(floor(camPos.z / terrain::constants::PageSize)) + 32);
	}

	void WorldState::EnsurePageIsLoaded(PagePosition pos)
	{
		auto *page = m_worldInstance->GetTerrain()->GetPage(pos.x(), pos.y());
		if (!page || !page->IsLoadable())
		{
			// Page not known, already loaded or not loadable yet
			return;
		}

		const bool loaded = page->Load();
		if (!loaded)
		{
			m_dispatcher.post([pos, this]()
							  { EnsurePageIsLoaded(pos); });
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
	void WorldState::OnRenderShadowsChanged(ConsoleVar &var, const std::string &oldValue)
	{
		if (m_skyComponent)
		{
			Light *sunLight = m_skyComponent->GetSunLight();
			if (sunLight)
			{
				sunLight->SetCastShadows(var.GetBoolValue());
			}
		}
	}

	void WorldState::OnShadowBiasChanged(ConsoleVar &var, const std::string &oldValue)
	{
		WorldFrame *worldFrame = WorldFrame::GetWorldFrame();
		if (!worldFrame)
		{
			WLOG("World frame not found");
			return;
		}

		const WorldRenderer *renderer = reinterpret_cast<const WorldRenderer *>(worldFrame->GetRenderer());
		if (!renderer)
		{
			WLOG("World frame has no renderer");
			return;
		}

		DeferredRenderer *deferred = renderer->GetDeferredRenderer();
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

	void WorldState::OnShadowTextureSizeChanged(ConsoleVar &var, const std::string &oldValue)
	{
		WorldFrame *worldFrame = WorldFrame::GetWorldFrame();
		if (!worldFrame)
		{
			WLOG("World frame not found");
			return;
		}

		const WorldRenderer *renderer = reinterpret_cast<const WorldRenderer *>(worldFrame->GetRenderer());
		if (!renderer)
		{
			WLOG("World frame has no renderer");
			return;
		}

		DeferredRenderer *deferred = renderer->GetDeferredRenderer();
		if (!deferred)
		{
			WLOG("Deferred renderer not initialized");
			return;
		}

		const uint16 s_shadowTexSizes[] = {
			512,
			1024,
			2048,
			4096 };

		const uint16 shadowTextureSize = s_shadowTexSizes[Clamp(s_shadowTextureSizeVar->GetIntValue(), 0, 3)];
		ILOG("Updating shadow texture size to " << shadowTextureSize << "x" << shadowTextureSize);
		deferred->SetShadowMapSize(shadowTextureSize);
	}

	void WorldState::OnShadowQualityChanged(ConsoleVar &var, const std::string &oldValue)
	{
		WorldFrame *worldFrame = WorldFrame::GetWorldFrame();
		if (!worldFrame)
		{
			WLOG("World frame not found");
			return;
		}

		const WorldRenderer *renderer = reinterpret_cast<const WorldRenderer *>(worldFrame->GetRenderer());
		if (!renderer)
		{
			WLOG("World frame has no renderer");
			return;
		}

		DeferredRenderer *deferred = renderer->GetDeferredRenderer();
		if (!deferred)
		{
			WLOG("Deferred renderer not initialized");
			return;
		}

		const int level = Clamp(var.GetIntValue(), 0, 2);
		ILOG("Updating shadow quality to level " << level);
		deferred->SetShadowQuality(level);
	}

	void WorldState::OnShadowTemporalChanged(ConsoleVar &var, const std::string &oldValue)
	{
		WorldFrame *worldFrame = WorldFrame::GetWorldFrame();
		if (!worldFrame)
		{
			WLOG("World frame not found");
			return;
		}

		const WorldRenderer *renderer = reinterpret_cast<const WorldRenderer *>(worldFrame->GetRenderer());
		if (!renderer)
		{
			WLOG("World frame has no renderer");
			return;
		}

		DeferredRenderer *deferred = renderer->GetDeferredRenderer();
		if (!deferred)
		{
			WLOG("Deferred renderer not initialized");
			return;
		}

		const bool enabled = var.GetIntValue() != 0;
		ILOG("Temporal shadow staggering " << (enabled ? "enabled" : "disabled"));
		deferred->SetTemporalShadowsEnabled(enabled);
	}

	void WorldState::OnDepthPrepassChanged(ConsoleVar &var, const std::string &oldValue)
	{
		WorldFrame *worldFrame = WorldFrame::GetWorldFrame();
		if (!worldFrame)
		{
			WLOG("World frame not found");
			return;
		}

		const WorldRenderer *renderer = reinterpret_cast<const WorldRenderer *>(worldFrame->GetRenderer());
		if (!renderer)
		{
			WLOG("World frame has no renderer");
			return;
		}

		DeferredRenderer *deferred = renderer->GetDeferredRenderer();
		if (!deferred)
		{
			WLOG("Deferred renderer not initialized");
			return;
		}

		const bool enabled = var.GetIntValue() != 0;
		ILOG("Depth pre-pass " << (enabled ? "enabled" : "disabled"));
		deferred->SetDepthPrepassEnabled(enabled);
	}

	void WorldState::OnFoliageEnabledChanged(ConsoleVar &var, const std::string &oldValue)
	{
		if (m_foliage)
		{
			const bool enabled = var.GetIntValue() != 0;
			m_foliage->SetVisible(enabled);
			ILOG("Foliage rendering " << (enabled ? "enabled" : "disabled"));

			m_foliage->RebuildAll();
		}
	}

	void WorldState::OnFoliageDensityChanged(ConsoleVar &var, const std::string &oldValue)
	{
		if (m_foliage)
		{
			FoliageSettings settings = m_foliage->GetSettings();
			settings.globalDensityMultiplier = Clamp(var.GetFloatValue(), 0.1f, 1.0f);
			m_foliage->SetSettings(settings);
			ILOG("Foliage density set to " << settings.globalDensityMultiplier);

			m_foliage->RebuildAll();
		}
	}

	void WorldState::OnTerrainLodEnabledChanged(ConsoleVar& var, const std::string& oldValue)
	{
		if (!m_worldInstance || !m_worldInstance->GetTerrain())
		{
			return;
		}

		m_worldInstance->GetTerrain()->SetLodEnabled(var.GetBoolValue());
	}

	void WorldState::OnTerrainOcclusionCullingChanged(ConsoleVar& var, const std::string& oldValue)
	{
		if (!m_worldInstance || !m_worldInstance->GetTerrain())
		{
			return;
		}

		m_worldInstance->GetTerrain()->SetOcclusionCullingEnabled(var.GetBoolValue());
	}

	void WorldState::GetPlayerName(uint64 guid, std::weak_ptr<GamePlayerC> player)
	{
		m_cache.GetNameCache().Get(guid, [player](uint64, const String &name)
								   {
			if (const std::shared_ptr<GamePlayerC> strong = player.lock())
			{
				strong->SetName(name);
			} });
	}

	void WorldState::GetCreatureData(uint64 guid, std::weak_ptr<GameUnitC> creature)
	{
		m_cache.GetCreatureCache().Get(guid, [creature](uint64, const CreatureInfo &data)
									   {
			if (const std::shared_ptr<GameUnitC> strong = creature.lock())
			{
				strong->SetCreatureInfo(data);
			} });
	}

	void WorldState::GetItemData(uint64 guid, std::weak_ptr<GameItemC> item)
	{
		m_cache.GetItemCache().Get(guid, [item](uint64, const ItemInfo &data)
								   {
				if (const std::shared_ptr<GameItemC> strong = item.lock())
				{
					strong->NotifyItemData(data);
				} });
	}

	void WorldState::GetItemData(uint64 guid, std::weak_ptr<GamePlayerC> player)
	{
		m_cache.GetItemCache().Get(guid, [player](uint64, const ItemInfo &data)
								   {
				if (const std::shared_ptr<GamePlayerC> strong = player.lock())
				{
					strong->NotifyItemData(data);
				} });
	}

	void WorldState::OnMoveEvent(GameUnitC &unit, const MovementEvent &moveEvent)
	{
		if (&unit != m_playerController->GetControlledUnit().get())
		{
			return;
		}

		m_playerController->ProcessMovementEvent(moveEvent);

		// Check for area trigger overlaps
		const Vector3 playerPosition = unit.GetSceneNode()->GetDerivedPosition();
		std::vector<uint32> newlyEnteredTriggers;
		m_areaTriggerManager.CheckForTriggerOverlap(playerPosition, newlyEnteredTriggers);

		// Send network packets for newly entered triggers
		for (const uint32 triggerId : newlyEnteredTriggers)
		{
			m_realmConnector.SendAreaTriggerTriggered(triggerId);
			DLOG("Player entered area trigger " << triggerId);
		}
	}

	bool WorldState::QueryWaterAt(const float x, const float z, float &outSurfaceY) const
	{
		if (!m_worldInstance)
		{
			return false;
		}

		terrain::Terrain *terrain = m_worldInstance->GetTerrain();
		if (!terrain || !terrain->HasWaterAtWorldPos(x, z))
		{
			return false;
		}

		outSurfaceY = terrain->GetWaterHeightAtWorldPos(x, z);
		return true;
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

		m_cache.GetGuildCache().Get(guildGuid, [player, this](uint64, const GuildInfo &info)
									{
				if (const auto strong = player.lock())
				{
					strong->NotifyGuildInfo(&info);
				} });
	}

	void WorldState::GetObjectData(uint64 guid, std::weak_ptr<GameWorldObjectC> object)
	{
		m_cache.GetObjectCache().Get(guid, [object](uint64, const ObjectInfo &data)
									 {
				if (const std::shared_ptr<GameWorldObjectC> strong = object.lock())
				{
					strong->NotifyObjectData(data);
				} });
	}

	// Required implementation of OnGameTimeInfo packet handler for WorldState
	PacketParseResult WorldState::OnGameTimeInfo(game::IncomingPacket &packet)
	{
		GameTime gameTime;
		float timeSpeed;

		if (!(packet >> io::read<uint64>(gameTime) >> io::read<float>(timeSpeed)))
		{
			ELOG("Failed to read GameTimeInfo packet!");
			return PacketParseResult::Disconnect;
		}

		// Update our game time component with server values
		m_gameTime.SetTime(gameTime);
		m_gameTime.SetTimeSpeed(timeSpeed);

		// Setup sky component
		m_skyComponent->SetNormalizedTimeOfDay(m_gameTime.GetNormalizedTimeOfDay());

		// Trigger a Lua event to notify the UI about the time change
		FrameManager::Get().TriggerLuaEvent("GAME_TIME_UPDATED",
											m_gameTime.GetHour(),
											m_gameTime.GetMinute(),
											m_gameTime.GetSecond(),
											m_gameTime.GetTimeSpeed());

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSetProficiency(game::IncomingPacket &packet)
	{
		uint32 proficiencyId;
		uint8 added;

		if (!(packet >> io::read<uint32>(proficiencyId) >> io::read<uint8>(added)))
		{
			ELOG("Failed to read SetProficiency packet!");
			return PacketParseResult::Disconnect;
		}

		auto player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		if (added)
		{
			player->AddProficiency(proficiencyId);
		}
		else
		{
			player->RemoveProficiency(proficiencyId);
		}

		// Log proficiency for character
		ILOG("Proficiency " << proficiencyId << (added ? " added" : " removed"));

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnTimePlayedResponse(game::IncomingPacket &packet)
	{
		uint32 timePlayed;
		if (!(packet >> io::read<uint32>(timePlayed)))
		{
			ELOG("Failed to read TimePlayedResponse packet!");
			return PacketParseResult::Disconnect;
		}

		// Update the UI or perform any necessary actions with the received time played value
		FrameManager::Get().TriggerLuaEvent("TIME_PLAYED_UPDATED", timePlayed);

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnTimeSyncRequest(game::IncomingPacket &packet)
	{
		uint32 syncIndex;
		if (!(packet >> io::read<uint32>(syncIndex)))
		{
			ELOG("Failed to read TimeSyncRequest packet!");
			return PacketParseResult::Disconnect;
		}

		// Get the current client timestamp
		const GameTime clientTimestamp = GetAsyncTimeMs();

		// Send the response back to the server with the sync index and client timestamp
		m_realmConnector.SendTimeSyncResponse(syncIndex, clientTimestamp);

		DLOG("Received TimeSyncRequest with index: " << syncIndex << ", responding with client time: " << clientTimestamp);
		m_timeSyncResponseSent = true;

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnSpellModChanged(game::IncomingPacket& packet)
	{
		uint8 modType, effectIndex, modOp;
		int32 value;
		if (!(packet
			>> io::read<uint8>(modType)
			>> io::read<uint8>(effectIndex)
			>> io::read<uint8>(modOp)
			>> io::read<int32>(value)))
		{
			ELOG("Failed to read SpellModChanged packet!");
			return PacketParseResult::Disconnect;
		}

		// Apply to the local controlled player
		const auto player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return PacketParseResult::Pass;
		}

		player->SetSpellMod(modType, effectIndex, modOp, value);

		// Notify the UI so tooltips can refresh
		FrameManager::Get().TriggerLuaEvent("SPELL_MOD_CHANGED", modOp);

		return PacketParseResult::Pass;
	}
}
