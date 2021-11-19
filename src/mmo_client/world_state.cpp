// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "world_state.h"
#include "game_state_mgr.h"

#include "realm_connector.h"
#include "console.h"
#include "login_state.h"
#include "world_frame.h"
#include "world_renderer.h"

#include "assets/asset_registry.h"
#include "frame_ui/frame_mgr.h"


namespace mmo
{
	const std::string WorldState::Name = "world";

	extern CharacterView s_selectedCharacter;
	
	WorldState::WorldState(RealmConnector& realmConnector)
		: m_realmConnector(realmConnector)
	{
	}

	void WorldState::OnEnter()
	{
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
		auto topFrame = FrameManager::Get().CreateOrRetrieve("Frame", "TopGameFrame");
		topFrame->SetAnchor(anchor_point::Left, anchor_point::Left, nullptr);
		topFrame->SetAnchor(anchor_point::Top, anchor_point::Top, nullptr);
		topFrame->SetAnchor(anchor_point::Right, anchor_point::Right, nullptr);
		topFrame->SetAnchor(anchor_point::Bottom, anchor_point::Bottom, nullptr);
		FrameManager::Get().SetTopFrame(topFrame);

		// Load ui file
		FrameManager::Get().LoadUIFile("Interface/GameUI/GameUI.toc");

		m_realmConnections += m_realmConnector.EnterWorldFailed.connect(*this, &WorldState::OnEnterWorldFailed);

		// Send enter world request to server
		m_realmConnector.EnterWorld(s_selectedCharacter);

		// Register drawing of the game ui
		m_paintLayer = Screen::AddLayer(std::bind(&WorldState::OnPaint, this), 1.0f, ScreenLayerFlags::IdentityTransform);
	}

	void WorldState::OnLeave()
	{
		// Disconnect all active connections
		m_realmConnections.disconnect();

		// Reset the logo frame ui
		FrameManager::Get().ResetTopFrame();
		
		// Remove world renderer
		FrameManager::Get().RemoveFrameRenderer("WorldRenderer");
		FrameManager::Get().UnregisterFrameFactory("World");
		
		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);
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
		FrameManager::Get().TriggerLuaEvent("REALM_DISCONNECTED");

		// Go back to login state
		GameStateMgr::Get().SetGameState(LoginState::Name);
	}

	void WorldState::OnEnterWorldFailed(game::player_login_response::Type error)
	{
		GameStateMgr::Get().SetGameState(LoginState::Name);
	}
}
