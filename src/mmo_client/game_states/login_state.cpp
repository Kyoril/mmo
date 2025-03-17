// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "login_state.h"

#include "game_state_mgr.h"
#include "loading_screen.h"
#include "platform.h"
#include "world_state.h"
#include "console/console.h"
#include "console/console_var.h"
#include "net/login_connector.h"
#include "net/realm_connector.h"

#include "assets/asset_registry.h"
#include "base/clock.h"
#include "base/timer_queue.h"
#include "frame_ui/frame_mgr.h"


namespace mmo
{
	extern ConsoleVar* s_lastRealmVar;

	uint32 g_mapId = 0;

	const std::string LoginState::Name = "login";
	
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
		RealmConnector& realmConnector, TimerQueue& timers, IAudio& audio)
		: GameState(gameStateManager)
		, m_loginConnector(loginConnector)
		, m_realmConnector(realmConnector)
		, m_timers(timers)
		, m_audio(audio)
	{
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

		// Load ui file
		frameMgr.LoadUIFile("Interface/GlueUI/GlueUI.toc");

		// Register drawing of the login ui
		m_paintLayer = Screen::AddLayer(std::bind(&LoginState::OnPaint, this), 1.0f, ScreenLayerFlags::IdentityTransform);

		// Register for signals of the login connector instance
		m_loginConnections += m_loginConnector.AuthenticationResult.connect(*this, &LoginState::OnAuthenticationResult);
		m_loginConnections += m_loginConnector.RealmListUpdated.connect(*this, &LoginState::OnRealmListUpdated);

		// Register for signals of the realm connector instance
		m_loginConnections += m_realmConnector.AuthenticationResult.connect(*this, &LoginState::OnRealmAuthenticationResult);
		m_loginConnections += m_realmConnector.CharListUpdated.connect(*this, &LoginState::OnCharListUpdated);

		m_loginConnections += m_realmConnector.Disconnected.connect(*this, &LoginState::OnRealmDisconnected);

		// Lets setup a test command
		Console::RegisterCommand("login", [&](const String& command, const String& args)
			{
				ConsoleCommand_Login(command, args);
			}, ConsoleCommandCategory::Debug, "Attempts to login with the given account name and password.");

		if (m_realmConnector.IsConnected())
		{
			// TODO: Get real reason string if there is more than this one!
			FrameManager::Get().TriggerLuaEvent("ENTER_WORLD_FAILED", "WORLD_SERVER_DOWN");
		}

		// Play background music
		m_musicSound = m_audio.CreateLoopedStream("Sound/Music/Genesis.ogg");
		m_audio.PlaySound(m_musicSound, &m_musicChannel);
	}

	void LoginState::OnLeave()
	{
		m_audio.StopSound(&m_musicChannel);
		m_musicChannel = InvalidChannel;
		m_musicSound = InvalidSound;

		Console::UnregisterCommand("login");

		m_realmConnector.ClearPacketHandler(game::realm_client_packet::CharCreateResponse);

		// Disconnect all active connections
		m_loginConnections.disconnect();

		m_loginConnector.resetListener();
		m_loginConnector.close();

		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);
		
		FrameManager::Get().ResetTopFrame();
	}

	std::string_view LoginState::GetName() const
	{
		return Name;
	}

	void LoginState::EnterWorld(const CharacterView& character)
	{
		// TODO: Load data
		LoadingScreen::Show();

		// Send packet
		m_realmConnector.EnterWorld(character);
		GameStateMgr::Get().SetGameState(WorldState::Name);
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
		}
		else
		{
			// Setup realm connector for connection
			m_realmConnector.SetLoginData(m_loginConnector.GetAccountName(), m_loginConnector.GetSessionKey());

			// Successfully authenticated
			FrameManager::Get().TriggerLuaEvent("AUTH_SUCCESS");
		}
	}

	void LoginState::OnCharListUpdated()
	{
		FrameManager::Get().TriggerLuaEvent("CHAR_LIST");
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

	void LoginState::OnRealmDisconnected()
	{
		m_realmConnector.ClearPacketHandler(game::realm_client_packet::CharCreateResponse);

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
		const int32 lastRealmId = s_lastRealmVar == nullptr ? -1 : s_lastRealmVar->GetIntValue();
		if (lastRealmId >= 0)
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

			FrameManager::Get().TriggerLuaEvent("REALM_AUTH_SUCCESS");
		}
	}
}
