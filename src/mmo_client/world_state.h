// Copyright (C) 2021, Robin Klimonow. All rights reserved.

#pragma once

#include "game_state.h"
#include "screen.h"

#include "frame_ui/frame_mgr.h"
#include "base/signal.h"


namespace mmo
{
	class LoginConnector;
	class RealmConnector;

	/// This class represents the initial game state where the player is asked to enter
	/// his credentials in order to authenticate.
	class WorldState final
		: public IGameState
	{
	public:
		explicit WorldState(RealmConnector& realmConnector);

	public:
		/// The default name of the world state
		static const std::string Name;

	public:
		// Inherited via IGameState
		void OnEnter() override;
		void OnLeave() override;
		const std::string& GetName() const override;

	private:
		/// Called when the screen layer should be painted. Should paint the scene.
		void OnPaint();
		// 
		void OnRealmDisconnected();

	private:
		
		RealmConnector& m_realmConnector;
		ScreenLayerIt m_paintLayer;
		scoped_connection_container m_realmConnections;
	};
}
