// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shared/audio/audio.h"
#include "connection.h"
#include "game_states/game_state.h"

#include "screen.h"

#include "auth_protocol/auth_protocol.h"
#include "game_protocol/game_protocol.h"
#include "base/signal.h"
#include "frame_ui/frame_mgr.h"

namespace mmo
{
	class TimerQueue;
	class CharacterView;
	class LoginConnector;
	class RealmConnector;
	class Discord;

	/// This class represents the initial game state where the player is asked to enter
	/// his credentials in order to authenticate.
	class LoginState final
		: public GameState
	{
	public:
		explicit LoginState(GameStateMgr& gameStateManager, LoginConnector& loginConnector, RealmConnector& realmConnector, TimerQueue& timers, IAudio& audio, Discord& discord);

	public:
		/// The default name of the login state
		static const std::string Name;

	public:
		// Inherited via IGameState
		void OnEnter() override;

		void OnLeave() override;

		[[nodiscard]] std::string_view GetName() const override;
		
	private:
		/// Called when the screen layer should be painted. Should paint the scene.
		void OnPaint();

		/// 
		void OnAuthenticationResult(auth::AuthResult result);

		/// 
		void OnRealmListUpdated();


		void OnRealmAuthenticationResult(uint8 result);

		/// 
		void OnCharListUpdated();

		/// 
		PacketParseResult OnCharCreationResponse(game::IncomingPacket& packet);

		// 
		void OnRealmDisconnected();


		void QueueRealmListRequestTimer();


		void OnRealmListTimer();
		
		void ConsoleCommand_Login(const std::string& cmd, const std::string& arguments);

	private:

		LoginConnector& m_loginConnector;
		RealmConnector& m_realmConnector;
		ScreenLayerIt m_paintLayer;
		scoped_connection_container m_loginConnections;
		TimerQueue& m_timers;
		IAudio& m_audio;
		Discord& m_discord;

		SoundIndex m_musicSound = InvalidSound;
		ChannelIndex m_musicChannel = InvalidChannel;
	};
}
