// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "realm_data.h"

#include "game_protocol/game_connector.h"
#include "base/big_number.h"
#include "base/signal.h"
#include "game/character_view.h"

#include "asio/io_service.hpp"


namespace mmo
{
	/// This class manages the connection to the current realm server if there is any.
	class RealmConnector final
		: public game::Connector
		, public game::IConnectorListener
	{
	public:
		/// Signal that is fired when the client successfully authenticated at the realm list.
		signal<void(uint8)> AuthenticationResult;
		/// Signal that is fired when the client received a new character list packet.
		signal<void()> CharListUpdated;

		signal<void()> Disconnected;

	private:
		// Internal io service
		asio::io_service& m_ioService;

		std::string m_realmAddress;
		uint16 m_realmPort;
		std::string m_realmName;
		std::string m_account;
		BigNumber m_sessionKey;
		uint32 m_serverSeed;
		uint32 m_clientSeed;

	public:
		/// Initializes a new instance of the RealmConnector class.
		/// @param io The io service to be used in order to create the internal socket.
		explicit RealmConnector(asio::io_service &io);

	private:
		/// Handles the LogonChallenge packet.
		PacketParseResult OnAuthChallenge(game::IncomingPacket& packet);
		/// Handles the AuthSessionResponse packet.
		PacketParseResult OnAuthSessionResponse(game::IncomingPacket& packet);
		/// Handles the CharEnum packet.
		PacketParseResult OnCharEnum(game::IncomingPacket& packet);

	public:
		// ~ Begin IConnectorListener
		bool connectionEstablished(bool success) override;
		void connectionLost() override;
		void connectionMalformedPacket() override;
		PacketParseResult connectionPacketReceived(game::IncomingPacket &packet) override;
		// ~ End IConnectorListener

	public:
		/// Sets login data
		void SetLoginData(const std::string& accountName, const BigNumber& sessionKey);
		/// 
		void ConnectToRealm(const RealmData& data);
		/// Tries to connect to the given realm server.
		/// @param realmAddress The ip address of the realm.
		/// @param realmPort The port of the realm.
		/// @param accountName Name of the player account.
		/// @param realmName The realm's display name.
		/// @param sessionKey The session key.
		void Connect(const std::string& realmAddress, uint16 realmPort, const std::string& accountName, const std::string& realmName, BigNumber sessionKey);
		/// Sends an enter world request using the given character.
		void EnterWorld(const CharacterView& character);

		/// Gets the realm name.
		const std::string& GetRealmName() const { return m_realmName; }

	public:
		/// Gets a constant list of character views.
		const std::vector<CharacterView>& GetCharacterViews() const { return m_characterViews; }

	private:
		/// A list of character views.
		std::vector<CharacterView> m_characterViews;
	};
}

