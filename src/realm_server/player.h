// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "player_manager.h"

#include "base/non_copyable.h"
#include "game_protocol/game_protocol.h"
#include "game_protocol/game_connection.h"
#include "base/big_number.h"

#include <memory>
#include <functional>
#include <map>
#include <cassert>

#include "login_connector.h"
#include "game/character_data.h"
#include "game/character_view.h"


namespace mmo
{
	class AsyncDatabase;
	class LoginConnector;
	class WorldManager;
	class World;


	/// This class represents a player connction on the login server.
	class Player final
		: public NonCopyable
		, public game::IConnectionListener
		, public std::enable_shared_from_this<Player>
	{
	public:
		typedef game::EncryptedConnection<game::Protocol> Client;
		typedef std::function<PacketParseResult(game::IncomingPacket &)> PacketHandler;

	public:
		explicit Player(
			PlayerManager &manager,
			WorldManager& worldManager,
			LoginConnector &loginConnector,
			AsyncDatabase &database,
			std::shared_ptr<Client> connection,
			const std::string &address);

		/// Gets the player connection class used to send packets to the client.
		Client &GetConnection() { assert(m_connection); return *m_connection; }

		/// Gets the player manager which manages all connected players.
		PlayerManager &GetManager() const { return m_manager; }

		/// Gets the world manager which manages all connected world nodes.
		WorldManager& GetWorldManager() const { return m_worldManager; }

		[[nodiscard]] bool HasCharacterGuid() const noexcept { return m_characterData.has_value(); }

		[[nodiscard]] uint64 GetCharacterGuid() const noexcept { return m_characterData.has_value() ? m_characterData.value().characterId : 0; }

		/// Determines whether the player is authentificated.
		/// @returns true if the player is authentificated.
		bool IsAuthentificated() const { return !m_sessionKey.isZero(); }

		/// Gets the account name the player is logged in with.
		const std::string &GetAccountName() const { return m_accountName; }

	public:
		/// Send an auth challenge packet to the client in order to ask it for authentication data.
		void SendAuthChallenge();

		/// Initializes the session by providing a session key. The connection to the client will 
		/// be encrypted from here on.
		void InitializeSession(const BigNumber& sessionKey);
		
		void SendProxyPacket(uint16 packetId, const std::vector<char> &buffer);

	private:
		/// Enables or disables handling of EnterWorld packets from the client.
		void EnableEnterWorldPacket(bool enable);

		void EnableProxyPackets(bool enable);

		void JoinWorld() const;

		void OnWorldJoined(const InstanceId instanceId);

		void OnWorldJoinFailed(const game::player_login_response::Type response);

		void OnCharacterData(std::optional<CharacterData> characterData);

		void OnWorldDestroyed(World& world);

		void NotifyWorldNodeChanged(World* worldNode);

	public:
		/// Registers a packet handler.
		void RegisterPacketHandler(uint16 opCode, PacketHandler &&handler);

		/// Syntactic sugar implementation of RegisterPacketHandler to avoid having to use std::bind.
		template <class Instance, class Class, class... Args1>
		void RegisterPacketHandler(uint16 opCode, Instance& object, PacketParseResult(Class::*method)(Args1...))
		{
			RegisterPacketHandler(opCode, [&object, method](Args1... args) {
				return (object.*method)(Args1(args)...);
			});
		}

		/// Clears a packet handler so that the opcode is no longer handled.
		void ClearPacketHandler(uint16 opCode);

	private:
		PlayerManager &m_manager;
		WorldManager &m_worldManager;
		LoginConnector &m_loginConnector;
		AsyncDatabase &m_database;
		std::shared_ptr<Client> m_connection;
		std::string m_address;						// IP address in string format
		std::string m_accountName;					// Account name in uppercase letters
		uint32 m_build;								// Build version: 0.0.0.XXXXX
		std::map<uint16, PacketHandler> m_packetHandlers;
		std::mutex m_packetHandlerMutex;
		uint32 m_seed;								// Random generated seed used for packet header encryption
		uint32 m_clientSeed;
		uint64 m_accountId;
		SHA1Hash m_clientHash;
		/// Session key of the game client, retrieved by login server on successful login request.
		BigNumber m_sessionKey;

		std::mutex m_charViewMutex;
		std::map<uint64, CharacterView> m_characterViews;

		std::weak_ptr<World> m_world;

		std::optional<CharacterData> m_characterData;
		scoped_connection m_worldDestroyed;

	private:
		/// Closes the connection if still connected.
		void Destroy();
		/// Requests the current character list from the database and notifies the client.
		void DoCharEnum();
	
	private:
		/// @copydoc mmo::auth::IConnectionListener::connectionLost()
		void connectionLost() override;

		/// @copydoc mmo::auth::IConnectionListener::connectionMalformedPacket()
		void connectionMalformedPacket() override;

		/// @copydoc mmo::auth::IConnectionListener::connectionPacketReceived()
		PacketParseResult connectionPacketReceived(game::IncomingPacket &packet) override;

	private:
		PacketParseResult OnAuthSession(game::IncomingPacket& packet);
		PacketParseResult OnCharEnum(game::IncomingPacket& packet);
		PacketParseResult OnEnterWorld(game::IncomingPacket& packet);
		PacketParseResult OnCreateChar(game::IncomingPacket& packet);
		PacketParseResult OnDeleteChar(game::IncomingPacket& packet);
		PacketParseResult OnProxyPacket(game::IncomingPacket& packet);
	};
}
