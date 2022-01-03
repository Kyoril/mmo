// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "login_state.h"
#include "login_connector.h"
#include "realm_connector.h"
#include "console.h"
#include "console_var.h"
#include "game_state_mgr.h"
#include "loading_screen.h"
#include "world_state.h"

#include "assets/asset_registry.h"
#include "base/clock.h"
#include "base/timer_queue.h"
#include "frame_ui/frame_mgr.h"


namespace mmo
{
	extern ConsoleVar* s_lastRealmVar;

	const std::string LoginState::Name = "login";
	
	LoginState::LoginState(LoginConnector & loginConnector, RealmConnector & realmConnector, TimerQueue& timers)
		: m_loginConnector(loginConnector)
		, m_realmConnector(realmConnector)
		, m_timers(timers)
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
	}

	void LoginState::OnLeave()
	{
		// Disconnect all active connections
		m_loginConnections.disconnect();

		m_loginConnector.resetListener();
		m_loginConnector.close();

		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);
		
		FrameManager::Get().ResetTopFrame();
	}

	const std::string & LoginState::GetName() const
	{
		return LoginState::Name;
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

	void LoginState::OnAuthenticationResult(auth::AuthResult result)
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

	void LoginState::OnRealmDisconnected()
	{
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
	
	void LoginState::OnRealmAuthenticationResult(uint8 result)
	{
		if (result != auth::auth_result::Success)
		{
			ELOG("Error on realm authentication...");
			FrameManager::Get().TriggerLuaEvent("REALM_AUTH_FAILED", static_cast<int32>(result));
		}
		else
		{
			ASSERT(s_lastRealmVar);
			s_lastRealmVar->Set(static_cast<int32>(m_realmConnector.GetRealmId()));
		
			FrameManager::Get().TriggerLuaEvent("REALM_AUTH_SUCCESS");
		}
	}
}
