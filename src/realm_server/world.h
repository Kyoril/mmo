// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "world_manager.h"

#include "base/non_copyable.h"
#include "auth_protocol/auth_protocol.h"
#include "auth_protocol/auth_connection.h"
#include "base/big_number.h"
#include "game/game.h"

#include <memory>
#include <functional>
#include <map>
#include <vector>
#include <cassert>

#include "base/signal.h"
#include "game/character_data.h"
#include "game/chat_type.h"


namespace mmo
{
	class TimerQueue;
}

namespace mmo
{
	namespace proto
	{
		class Project;
	}

	class PlayerManager;
	class AsyncDatabase;

	/// Callback executed after a world join returned a result.
	typedef std::function<void(InstanceId instanceId, bool success)> JoinWorldCallback;

	/// This class represents a world node connection on the realm server.
	class World final
		: public NonCopyable
		, public auth::IConnectionListener
		, public std::enable_shared_from_this<World>
	{
	public:
		signal<void(World&)> destroyed;

	public:
		typedef mmo::Connection<auth::Protocol> Client;
		typedef std::function<PacketParseResult(auth::IncomingPacket &)> PacketHandler;

	public:
		explicit World(
			TimerQueue& timerQueue,
			WorldManager &manager,
			PlayerManager& playerManager,
			AsyncDatabase &database,
			std::shared_ptr<Client> connection,
			const std::string &address,
			const proto::Project& project);

		/// Gets the connection class used to send packets to the world node client.
		Client &GetConnection() const { assert(m_connection); return *m_connection; }

		/// Gets the world manager which manages all connected world instances.
		WorldManager &GetManager() const { return m_manager; }

		/// Gets the name of this world.
		const String& GetWorldName() const { return m_worldName; }

		void Join(CharacterData characterData, JoinWorldCallback callback);

		void Leave(ObjectGuid characterGuid);

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

	public:
		/// Determines whether a given map id is hosted by this world node.
		bool IsHostingMapId(MapId mapId);

		/// Determines whether a given instance id is hosted by this world node.
		bool IsHostingInstanceId(InstanceId instanceId);

		/// Requests the creation of an instance for the given map id.
		void RequestMapInstanceCreation(MapId mapId);

		/// Determines whether this world node is authenticated.
		bool IsAuthenticated() const { return !m_sessionKey.isZero(); }

		/// Sends a local chat message to the world on behalf of a player.
		void LocalChatMessage(uint64 playerGuid, ChatType chatType, const std::string& message) const;

	private:
		TimerQueue& m_timerQueue;
		WorldManager &m_manager;
		PlayerManager& m_playerManager;
		AsyncDatabase &m_database;
		std::shared_ptr<Client> m_connection;
		std::string m_address;						// IP address in string format
		std::map<uint16, PacketHandler> m_packetHandlers;
		std::mutex m_packetHandlerMutex;
		std::mutex m_hostedMapIdMutex;
		std::vector<MapId> m_hostedMapIds;
		std::mutex m_hostedInstanceIdMutex;
		std::vector<InstanceId> m_hostedInstanceIds;
		std::string m_worldName;
		uint64 m_worldId;
		uint8 m_version1;
		uint8 m_version2;
		uint8 m_version3;
		uint16 m_build;
		BigNumber m_sessionKey;
		BigNumber m_s, m_v;
		BigNumber m_b, m_B;
		BigNumber m_unk3;
		BigNumber m_reconnectProof;
		BigNumber m_reconnectKey;
		SHA1Hash m_m2;
		std::mutex m_joinCallbackMutex;
		std::map<uint64, JoinWorldCallback> m_joinCallbacks;
		const proto::Project& m_project;

	private:
		/// Closes the connection if still connected.
		void Destroy();

		void SendAuthProof(auth::AuthResult result);

		void ConsumeOnCharacterJoinedCallback(uint64 characterGuid, bool success, InstanceId instanceId);
	
	private:
		/// @copydoc mmo::auth::IConnectionListener::connectionLost()
		void connectionLost() override;

		/// @copydoc mmo::auth::IConnectionListener::connectionMalformedPacket()
		void connectionMalformedPacket() override;

		/// @copydoc mmo::auth::IConnectionListener::connectionPacketReceived()
		PacketParseResult connectionPacketReceived(auth::IncomingPacket &packet) override;
		
	private:
		/// Handles an incoming packet with packet id LogonChallenge.
		/// @param packet The packet data.
		PacketParseResult OnLogonChallenge(auth::IncomingPacket& packet);

		/// Handles an incoming packet with packet id LogonProof.
		/// @param packet The packet data.
		PacketParseResult OnLogonProof(auth::IncomingPacket& packet);

		/// Handles an incoming packet with packet id OnPropagateMapList.
		/// @param packet The packet data.
		PacketParseResult OnPropagateMapList(auth::IncomingPacket& packet);
		
		PacketParseResult OnPlayerCharacterJoined(auth::IncomingPacket& packet);

		PacketParseResult OnPlayerCharacterLeft(auth::IncomingPacket& packet);
		
		PacketParseResult OnPlayerCharacterJoinFailed(auth::IncomingPacket& packet);

		PacketParseResult OnInstanceCreated(auth::IncomingPacket& packet);

		PacketParseResult OnInstanceDestroyed(auth::IncomingPacket& packet);
		
		PacketParseResult OnProxyPacket(auth::IncomingPacket& packet);

		PacketParseResult OnCharacterData(auth::IncomingPacket& packet);
	};

}
