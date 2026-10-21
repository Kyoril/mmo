// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "login_state.h"

#include "discord.h"
#include "game_state_mgr.h"
#include "event_loop.h"
#include "loading_screen.h"
#include "platform.h"
#include "world_state.h"
#include "console/console.h"
#include "console/console_var.h"
#include "game_client/sound_entry_player.h"
#include "net/login_connector.h"
#include "net/realm_connector.h"
#include "char_creation/char_select.h"
#include "glue/glue_scene.h"
#include "game_client/plaza_roster.h"
#include "ui/world_renderer.h"

#include "assets/asset_registry.h"
#include "base/clock.h"
#include "base/timer_queue.h"
#include "frame_ui/frame_mgr.h"

#include <chrono>


namespace mmo
{
	extern ConsoleVar* s_lastRealmVar;

	uint32 g_mapId = 0;

	const std::string LoginState::Name = "login";
	LoginReturnReason LoginState::s_returnReason = LoginReturnReason::NormalLogout;
	std::optional<auth::SessionKickReason> LoginState::s_kickReason;
	
	/// This command will try to connect to the login server and make a login attempt using the
	/// first parameter as username and the other parameters as password.
	void LoginState::ConsoleCommand_Login(const std::string& cmd, const std::string& arguments)
	{
		const auto spacePos = arguments.find(' ');
		if (spacePos == std::string::npos)
		{
			ELOG("Invalid argument count!");
			return;
		}

		// Try to connect
		m_loginConnector.Connect(arguments.substr(0, spacePos), arguments.substr(spacePos + 1));
		
		FrameManager::Get().TriggerLuaEvent("LOGIN_CONNECT");
	}

	LoginState::LoginState(GameStateMgr& gameStateManager, LoginConnector& loginConnector,
		RealmConnector& realmConnector, TimerQueue& timers, IAudio& audio, SoundEntryPlayer& soundEntryPlayer, Discord& discord, const proto_client::Project& project, CharSelect& charSelect)
		: GameState(gameStateManager)
		, m_loginConnector(loginConnector)
		, m_realmConnector(realmConnector)
		, m_timers(timers)
		, m_audio(audio)
		, m_soundEntryPlayer(soundEntryPlayer)
		, m_discord(discord)
		, m_project(project)
		, m_charSelect(charSelect)
	{
	}

	LoginState::~LoginState() = default;

	void LoginState::CreateGlueScene()
	{
		constexpr const char* definitionFile = "Interface/GlueUI/GlueScene.json";

		const std::unique_ptr<std::istream> stream = AssetRegistry::OpenFile(definitionFile);
		if (!stream)
		{
			ELOG("Glue scene: could not open " << definitionFile);
			return;
		}

		const String text{ std::istreambuf_iterator<char>(*stream), std::istreambuf_iterator<char>() };
		std::optional<GlueSceneDefinition> definition = ParseGlueSceneDefinition(text);
		if (!definition)
		{
			ELOG("Glue scene: " << definitionFile << " is not a valid scene definition");
			return;
		}

		m_glueScene = std::make_unique<GlueScene>(m_project, std::move(*definition), m_soundEntryPlayer, m_audio);

		// Before a realm is chosen, the realm's default time applies: UTC time of day.
		const auto sinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
		m_glueScene->SetTimeOfDay(static_cast<GameTime>(sinceEpoch.count()) % constants::OneDay);
	}

	DeferredRenderer* LoginState::FindGlueSceneRenderer() const
	{
		const FramePtr frame = FrameManager::Get().Find("GlueSceneFrame");
		if (!frame)
		{
			return nullptr;
		}

		const auto* renderer = dynamic_cast<const WorldRenderer*>(frame->GetRenderer());
		return renderer ? renderer->GetDeferredRenderer() : nullptr;
	}

	void LoginState::OnIdle(const float deltaSeconds, GameTime timestamp)
	{
		if (!m_glueScene)
		{
			return;
		}

		m_glueScene->SetRenderer(FindGlueSceneRenderer());
		m_glueScene->Update(deltaSeconds);

		// The 3D listener sits at the camera, so the placed town ambience is heard from there.
		m_audio.Update(m_glueScene->GetCameraPosition(), deltaSeconds);

		if (!m_glueSceneReadySent && m_glueScene->IsGateReady())
		{
			m_glueSceneReadySent = true;
			ILOG("Glue scene: gate view ready after " << (GetAsyncTimeMs() - m_enterTime) << " ms");
			FrameManager::Get().TriggerLuaEvent("GLUE_SCENE_READY");
		}

		if (!m_plazaReadyLogged && m_glueScene->IsPlazaReady())
		{
			m_plazaReadyLogged = true;
			ILOG("Glue scene: plaza view ready after " << (GetAsyncTimeMs() - m_enterTime) << " ms");
		}

		if (m_glueScene->ConsumeFlightFinished())
		{
			// Lifts a held fade (the plaza snap after FadeWhenReady counts as a flight) and tells the
			// screens where the camera is now.
			const bool atPlaza = m_glueScene->GetDirector().GetMode() == GlueCameraMode::Plaza;
			FrameManager::Get().TriggerLuaEvent("GLUE_FADE_RELEASE");
			FrameManager::Get().TriggerLuaEvent("GLUE_FLIGHT_FINISHED", atPlaza ? "plaza" : "gate");
		}

		if (m_glueScene->ConsumePushInFinished())
		{
			FrameManager::Get().TriggerLuaEvent("GLUE_PUSHIN_FINISHED");
		}
	}

	void LoginState::OnEnter()
	{
		FrameManager& frameMgr = FrameManager::Get();
		
		// Make the top frame element
		const auto topFrame = frameMgr.CreateOrRetrieve("Frame", "TopFrame");
		topFrame->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr);
		topFrame->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr);
		topFrame->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr);
		topFrame->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr);
		frameMgr.SetTopFrame(topFrame);

		// The scene behind the glue screens. Its renderer must be known before the UI loads, as
		// GlueParent.xml creates the frame that uses it.
		m_enterTime = GetAsyncTimeMs();
		m_glueSceneReadySent = false;
		m_plazaReadyLogged = false;
		CreateGlueScene();
		if (m_glueScene)
		{
			GlueScene* const glueScene = m_glueScene.get();
			frameMgr.RegisterFrameRenderer("GlueSceneRenderer", [glueScene](const std::string& name)
				{
					return std::make_unique<WorldRenderer>(name, glueScene->GetScene());
				});
		}

		// Before the UI loads, so the character screen's first selection already finds the plaza.
		m_charSelect.SetGlueScene(m_glueScene.get());

		// Load ui file
		frameMgr.LoadUIFile("Interface/GlueUI/GlueUI.toc");

		if (!m_glueScene)
		{
			// Without a scene there is nothing to wait for: lift the dark fill at once.
			m_glueSceneReadySent = true;
			frameMgr.TriggerLuaEvent("GLUE_SCENE_READY");
		}

		// Register drawing of the login ui
		m_paintLayer = Screen::AddLayer(std::bind(&LoginState::OnPaint, this), 1.0f, ScreenLayerFlags::IdentityTransform);

		// Register for signals of the login connector instance
		m_loginConnections += m_loginConnector.AuthenticationResult.connect(*this, &LoginState::OnAuthenticationResult);
		m_loginConnections += m_loginConnector.RealmListUpdated.connect(*this, &LoginState::OnRealmListUpdated);
		m_loginConnections += m_loginConnector.Kicked.connect(*this, &LoginState::OnAccountKicked);

		// Register for signals of the realm connector instance
		m_loginConnections += m_realmConnector.AuthenticationResult.connect(*this, &LoginState::OnRealmAuthenticationResult);
		m_loginConnections += m_realmConnector.CharListUpdated.connect(*this, &LoginState::OnCharListUpdated);

		m_loginConnections += m_realmConnector.Disconnected.connect(*this, &LoginState::OnRealmDisconnected);

		// Lets setup a test command
		Console::RegisterCommand("login", [&](const String& command, const String& args)
			{
				ConsoleCommand_Login(command, args);
			}, ConsoleCommandCategory::Debug, "Attempts to login with the given account name and password.");

		m_charListReceived = false;
		Console::RegisterCommand("enterworld", [&](const String& command, const String& args)
			{
				ConsoleCommand_EnterWorld(command, args);
			}, ConsoleCommandCategory::Debug, "Enters the world with the selected character as soon as the character list is shown.");

		m_loginConnections += EventLoop::KeyDown.connect(this, &LoginState::OnKeyDown);
		m_loginConnections += EventLoop::MouseDown.connect([this](EMouseButton, int32, int32) { return OnMouseDown(); });
		m_loginConnections += EventLoop::Idle.connect(this, &LoginState::OnIdle);

		m_returningFromWorld = false;
		if (m_realmConnector.IsConnected())
		{
			// Request a fresh character list from the server; the UI shows a loading dialog
			// while waiting. On EnterWorldFailed, an error will be shown once the list loads.
			m_realmConnector.RequestCharacterList();

			// Back from the world: the plaza appears without a flight, so the scene stays covered
			// (and the gate view is never shown) until the plaza is ready.
			m_returningFromWorld = true;
			if (m_glueScene)
			{
				FrameManager::Get().TriggerLuaEvent("GLUE_FADE_HOLD");
			}

			switch (s_returnReason)
			{
			case LoginReturnReason::EnterWorldFailed:
				FrameManager::Get().TriggerLuaEvent("REQUEST_CHAR_LIST", "WORLD_SERVER_DOWN");
				break;
			case LoginReturnReason::NormalLogout:
			default:
				FrameManager::Get().TriggerLuaEvent("REQUEST_CHAR_LIST");
				break;
			}
		}
		else if (s_returnReason == LoginReturnReason::RealmDisconnected)
		{
			// Realm dropped while in-world — land on login screen and show an error. When the
			// server said why (an account signed in elsewhere, say), the reason is passed on so
			// the player gets that message instead of the generic one.
			if (s_kickReason)
			{
				FrameManager::Get().TriggerLuaEvent("GLUE_REALM_DISCONNECTED", static_cast<int32>(*s_kickReason));
			}
			else
			{
				FrameManager::Get().TriggerLuaEvent("GLUE_REALM_DISCONNECTED");
			}
		}

		// Reset reason so subsequent re-entries default to no error.
		s_returnReason = LoginReturnReason::NormalLogout;
		s_kickReason.reset();

		// Play background music: a SoundEntry id can be configured through the LoginMusicSound
		// cvar; if unset or unresolvable, fall back to the built-in default so the login screen
		// is never silent.
		m_musicChannel = InvalidChannel;
		if (const ConsoleVar* loginMusicVar = ConsoleVarMgr::FindConsoleVar("LoginMusicSound"))
		{
			if (const uint32 soundId = static_cast<uint32>(loginMusicVar->GetIntValue()); soundId != 0)
			{
				m_musicChannel = m_soundEntryPlayer.PlayEntry(soundId);
			}
		}

		if (m_musicChannel == InvalidChannel)
		{
			m_musicSound = m_audio.CreateLoopedStream("Sound/Music/Genesis.ogg");
			m_audio.PlaySound(m_musicSound, &m_musicChannel, 1.0f, SoundCategory::Music);
		}

		// Ensure the loading screen is hidden
		LoadingScreen::Hide();
	}

	void LoginState::OnLeave()
	{
		m_audio.StopSound(&m_musicChannel);
		m_musicChannel = InvalidChannel;
		m_musicSound = InvalidSound;

		Console::UnregisterCommand("login");
		Console::UnregisterCommand("enterworld");
		m_enterWorldRequested = false;

		m_realmConnector.ClearPacketHandler(game::realm_client_packet::CharCreateResponse);

		// Disconnect all active connections
		m_loginConnections.disconnect();

		m_loginConnector.resetListener();
		m_loginConnector.close();

		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);

		// The frames own the renderer that draws the scene: it goes with them, the scene after them.
		FrameManager::Get().RemoveFrameRenderer("GlueSceneRenderer");
		if (m_glueScene)
		{
			m_glueScene->SetRenderer(nullptr);
		}

		FrameManager::Get().ResetTopFrame();

		// The plaza characters live in the scene: the character screen must not reach them anymore.
		m_charSelect.SetGlueScene(nullptr);
		m_glueScene.reset();
	}

	std::string_view LoginState::GetName() const
	{
		return Name;
	}

	void LoginState::OnPaint()
	{
		// Render the logo frame ui
		FrameManager::Get().Draw();
	}

	void LoginState::OnAuthenticationResult(const auth::AuthResult result)
	{
		if (result != auth::auth_result::Success)
		{
			FrameManager::Get().TriggerLuaEvent("AUTH_FAILED", static_cast<int32>(result));
			
			// Close the connection after notifying the UI about the error
			m_loginConnector.close();
		}
		else
		{
			// Setup realm connector for connection
			m_realmConnector.SetLoginData(m_loginConnector.GetAccountName(), m_loginConnector.GetSessionKey());

			// Only the first realm list after a login may jump straight into the last realm. Later
			// lists are periodic refreshes of a realm list the player is looking at (for example after
			// cancelling that very connection attempt) and must not pull them out of it.
			m_autoConnectToLastRealm = true;

			// Successfully authenticated
			FrameManager::Get().TriggerLuaEvent("AUTH_SUCCESS");
		}
	}

	void LoginState::OnCharListUpdated()
	{
		m_charSelect.OnCharacterListUpdated();

		const bool returningFromWorld = m_returningFromWorld;
		m_returningFromWorld = false;

		if (m_glueScene)
		{
			m_glueScene->SetTimeOfDay(m_realmConnector.GetRealmTimeOfDay());

			// Also runs for the refreshes after creating or deleting a character, where the camera
			// already is at the plaza: FlyIn and SnapToPlaza do nothing there.
			switch (DecideGlueArrival(m_glueScene->IsPlazaReady(), returningFromWorld))
			{
			case GlueArrival::FlyIn:
				m_glueScene->GetDirector().FlyIn();
				FrameManager::Get().TriggerLuaEvent("GLUE_FADE_RELEASE");
				break;
			case GlueArrival::Snap:
				m_glueScene->GetDirector().SnapToPlaza();
				FrameManager::Get().TriggerLuaEvent("GLUE_FADE_RELEASE");
				break;
			case GlueArrival::FadeWhenReady:
				// Covered until the plaza is ready, then cut to it (see OnIdle).
				m_glueScene->FadeToPlazaWhenReady();
				FrameManager::Get().TriggerLuaEvent("GLUE_FADE_HOLD");
				break;
			}
		}
		else
		{
			FrameManager::Get().TriggerLuaEvent("GLUE_FADE_RELEASE");
		}

		// After the camera decision, so the screen sees whether a flight is running.
		FrameManager::Get().TriggerLuaEvent("CHAR_LIST");

		m_charListReceived = true;
		TryEnterWorld();
	}

	void LoginState::ConsoleCommand_EnterWorld(const std::string& cmd, const std::string& arguments)
	{
		m_enterWorldRequested = true;
		TryEnterWorld();
	}

	void LoginState::TryEnterWorld()
	{
		if (!m_enterWorldRequested || !m_charListReceived || !m_realmConnector.IsConnected())
		{
			return;
		}

		m_enterWorldRequested = false;

		// The character screen decides: it knows the selection and whether that character may enter.
		FrameManager::Get().TriggerLuaEvent("ENTER_WORLD_REQUESTED");
	}

	bool LoginState::OnKeyDown(const int32 key, bool repeat)
	{
		if (m_glueScene && m_glueScene->IsFlightActive())
		{
			// Any key skips a flight, and does nothing else: Enter must not enter the world from
			// the middle of a flight.
			if (!repeat)
			{
				m_glueScene->GetDirector().Skip();
			}
			return true;
		}

		constexpr int32 enterKey = 0x0D;
		if (key == enterKey && !repeat && m_charListReceived && m_realmConnector.IsConnected())
		{
			FrameManager::Get().TriggerLuaEvent("ENTER_WORLD_REQUESTED");
		}

		// Screens without a focused text field (the realm list) drive keyboard navigation from Lua.
		FrameManager::Get().TriggerLuaEvent("GLUE_KEY_DOWN", key, repeat);

		return true;
	}

	bool LoginState::OnMouseDown()
	{
		if (!m_glueScene || !m_glueScene->IsFlightActive())
		{
			return false;
		}

		m_glueScene->GetDirector().Skip();
		return true;
	}

	PacketParseResult LoginState::OnCharCreationResponse(game::IncomingPacket& packet)
	{
		game::CharCreateResult result;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		FrameManager::Get().TriggerLuaEvent("CHAR_CREATION_FAILED", static_cast<uint8>(result));
		return PacketParseResult::Pass;
	}

	void LoginState::OnAccountKicked(const auth::SessionKickReason reason)
	{
		FrameManager::Get().TriggerLuaEvent("ACCOUNT_KICKED", static_cast<int32>(reason));
	}

	void LoginState::OnRealmDisconnected()
	{
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::CharCreateResponse);

		// Back to the login screen at the gate, without a flight.
		m_returningFromWorld = false;
		if (m_glueScene)
		{
			m_glueScene->SnapToGate();
			FrameManager::Get().TriggerLuaEvent("GLUE_FADE_RELEASE");
		}

		// The realm tells us why before closing the connection when it terminated the session
		// deliberately. Nothing to pass on for an ordinary drop.
		if (const auto kickReason = m_realmConnector.GetKickReason())
		{
			FrameManager::Get().TriggerLuaEvent("REALM_DISCONNECTED", static_cast<int32>(*kickReason));
			return;
		}

		FrameManager::Get().TriggerLuaEvent("REALM_DISCONNECTED");
	}

	void LoginState::QueueRealmListRequestTimer()
	{
		if (!m_loginConnector.IsConnected())
		{
			return;
		}

		if (m_realmConnector.IsConnected())
		{
			return;
		}
		
		m_timers.AddEvent(GetAsyncTimeMs() + constants::OneSecond * 10, *this, &LoginState::OnRealmListTimer);
	}

	void LoginState::OnRealmListTimer()
	{
		if (m_realmConnector.IsConnected())
		{
			return;
		}

		m_loginConnector.SendRealmListRequest();
	}

	void LoginState::OnRealmListUpdated()
	{
		const bool autoConnect = m_autoConnectToLastRealm;
		m_autoConnectToLastRealm = false;

		const int32 lastRealmId = s_lastRealmVar == nullptr ? -1 : s_lastRealmVar->GetIntValue();
		if (autoConnect && lastRealmId >= 0)
		{
			const auto& realms = m_loginConnector.GetRealms();
			const auto it = std::find_if(realms.begin(), realms.end(), [lastRealmId](const RealmData& realm)
			{
				return realm.id == static_cast<uint32>(lastRealmId);
			});

			if (it != realms.end())
			{
				ILOG("Connecting to last connected realm " << it->name << "...");
				m_realmConnector.ConnectToRealm(*it);

				FrameManager::Get().TriggerLuaEvent("CONNECTING_TO_REALM");
				return;
			}
		}
		
		DLOG("Refreshing realm list UI");

		FrameManager::Get().TriggerLuaEvent("REALM_LIST");
		QueueRealmListRequestTimer();

	}
	
	void LoginState::OnRealmAuthenticationResult(const uint8 result)
	{
		if (result != auth::auth_result::Success)
		{
			ELOG("Error on realm authentication...");
			FrameManager::Get().TriggerLuaEvent("REALM_AUTH_FAILED", static_cast<int32>(result));
		}
		else
		{
			m_realmConnector.RegisterPacketHandler(game::realm_client_packet::CharCreateResponse, *this, &LoginState::OnCharCreationResponse);

			ASSERT(s_lastRealmVar);
			s_lastRealmVar->Set(static_cast<int32>(m_realmConnector.GetRealmId()));

			m_discord.NotifyRealmChanged(m_realmConnector.GetRealmName());

			FrameManager::Get().TriggerLuaEvent("REALM_AUTH_SUCCESS");
		}
	}
}
