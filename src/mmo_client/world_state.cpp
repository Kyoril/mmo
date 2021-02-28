// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "world_state.h"
#include "game_state_mgr.h"

#include "realm_connector.h"
#include "console.h"
#include "login_state.h"

#include "assets/asset_registry.h"
#include "frame_ui/frame_mgr.h"


namespace mmo
{
	const std::string WorldState::Name = "world";

	static FrameManager& s_frameMgr = FrameManager::Get();


	WorldState::WorldState(RealmConnector& realmConnector)
		: m_realmConnector(realmConnector)
	{
	}

	void WorldState::OnEnter()
	{
		// Make the top frame element
		auto topFrame = s_frameMgr.CreateOrRetrieve("Frame", "TopGameFrame");
		topFrame->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr);
		topFrame->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr);
		topFrame->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr);
		topFrame->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr);
		s_frameMgr.SetTopFrame(topFrame);

		// Load ui file
		s_frameMgr.LoadUIFile("Interface/GameUI/GameUI.toc");

		// Register drawing of the game ui
		m_paintLayer = Screen::AddLayer(std::bind(&WorldState::OnPaint, this), 1.0f, ScreenLayerFlags::IdentityTransform);
	}

	void WorldState::OnLeave()
	{
		// Disconnect all active connections
		m_realmConnections.disconnect();

		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);

		// Reset the logo frame ui
		s_frameMgr.ResetTopFrame();
	}

	const std::string& WorldState::GetName() const
	{
		return WorldState::Name;
	}

	void WorldState::OnPaint()
	{
		FrameManager::Get().Draw();
	}
	
	void WorldState::OnRealmDisconnected()
	{
		// Trigger the lua event
		s_frameMgr.TriggerLuaEvent("REALM_DISCONNECTED");

		// Go back to login state
		GameStateMgr::Get().SetGameState(LoginState::Name);
	}
}
