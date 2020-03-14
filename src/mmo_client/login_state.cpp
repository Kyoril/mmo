// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_state.h"
#include "login_connector.h"
#include "realm_connector.h"
#include "console.h"

#include "assets/asset_registry.h"
#include "base/macros.h"
#include "base/utilities.h"
#include "log/default_log_levels.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/button.h"
#include "base/constants.h"

#include "asio.hpp"

#include <set>


namespace mmo
{
	const std::string LoginState::Name = "login";

	static FrameManager &s_frameMgr = FrameManager::Get();


	LoginState::LoginState(LoginConnector & loginConnector, RealmConnector & realmConnector)
		: m_loginConnector(loginConnector)
		, m_realmConnector(realmConnector)
	{
	}

	void LoginState::OnEnter()
	{
		// Make the logo frame element
		auto topFrame = s_frameMgr.CreateOrRetrieve("Frame", "TopFrame");
		topFrame->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr);
		topFrame->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr);
		topFrame->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr);
		topFrame->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr);
		s_frameMgr.SetTopFrame(topFrame);

		// Load ui file
		s_frameMgr.LoadUIFile("Interface/GlueUI/GlueUI.toc");

		// Register drawing of the login ui
		m_paintLayer = Screen::AddLayer(std::bind(&LoginState::OnPaint, this), 1.0f, ScreenLayerFlags::IdentityTransform);

		// Register for signals of the login connector instance
		m_loginConnections += m_loginConnector.AuthenticationResult.connect(*this, &LoginState::OnAuthenticationResult);
		m_loginConnections += m_loginConnector.RealmListUpdated.connect(*this, &LoginState::OnRealmListUpdated);

		// Register realm signal
		m_loginConnections += m_realmConnector.Authenticated.connect(*this, &LoginState::OnRealmAuthenticated);
	}

	void LoginState::OnLeave()
	{
		// Disconnect all active connections
		m_loginConnections.disconnect();

		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);

		// Reset the logo frame ui
		s_frameMgr.ResetTopFrame();

		// Reset texture
		m_texture.reset();
	}

	const std::string & LoginState::GetName() const
	{
		return LoginState::Name;
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
			// TODO: In case there was an error, update the UI to display an error message
		}
	}

	void LoginState::OnRealmListUpdated()
	{
		// TODO: We want to show a realm list to the user so he can choose a
		// realm to connect to. But for now, we will just connect with the first realm
		// available (if there is any).

		// Check if there are realms available
		const auto& realms = m_loginConnector.GetRealms();
		if (realms.empty())
		{
			DLOG("No realms available");
			return;
		}

		// Get the first realm
		const auto& realm = realms[0];
		ILOG("Connecting to realm '" << realm.name << "'...");

		// Connect to the first realm available
		m_realmConnector.Connect(
			realm.address, 
			realm.port, 
			m_loginConnector.GetAccountName(),
			realm.name, 
			m_loginConnector.GetSessionKey());
	}

	void LoginState::OnRealmAuthenticated()
	{
		DLOG("[Realm] Success!");


	}
}
