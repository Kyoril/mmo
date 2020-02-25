// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "game_state.h"
#include "screen.h"

#include "frame_ui/frame_mgr.h"
#include "graphics/graphics_device.h"
#include "base/signal.h"
#include "auth_protocol/auth_protocol.h"


namespace mmo
{
	class LoginConnector;
	class RealmConnector;

	/// This class represents the initial game state where the player is asked to enter
	/// his credentials in order to authenticate.
	class LoginState final
		: public IGameState
	{
	public:
		explicit LoginState(LoginConnector& loginConnector, RealmConnector& realmConnector);

	public:
		/// The default name of the login state
		static const std::string Name;

	public:
		// Inherited via IGameState
		virtual void OnEnter() override;
		virtual void OnLeave() override;
		virtual const std::string & GetName() const override;

	private:
		/// Called when the screen layer should be painted. Should paint the scene.
		void OnPaint();
		/// 
		void OnAuthenticationResult(auth::AuthResult result);
		/// 
		void OnRealmListUpdated();

	private:

		LoginConnector& m_loginConnector;
		RealmConnector& m_realmConnector;
		ScreenLayerIt m_paintLayer;
		TexturePtr m_texture;
		scoped_connection_container m_loginConnections;
	};
}
