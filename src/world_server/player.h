// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "realm_connector.h"
#include "vector_sink.h"
#include "game/character_data.h"
#include "game/chat_type.h"
#include "game/game_object_s.h"
#include "game/game_unit_s.h"
#include "game/tile_index.h"
#include "game/tile_subscriber.h"
#include "game_protocol/game_protocol.h"

namespace mmo
{
	namespace proto
	{
		class Project;
	}

	/// @brief This class represents the connection to a player on a realm server that this
	///	       world node is connected to.
	class Player final : public TileSubscriber
	{
	public:
		explicit Player(RealmConnector& realmConnector, std::shared_ptr<GameUnitS> characterObject,
		                CharacterData characterData, const proto::Project& project);
		~Player() override;

	public:
		/// @brief Sends a network packet directly to the client.
		/// @tparam F Type of the packet generator function.
		/// @param generator The packet generator function.
		template<class F>
		void SendPacket(F generator) const
		{
			std::vector<char> buffer;
			io::VectorSink sink(buffer);

			typename game::Protocol::OutgoingPacket packet(sink);
			generator(packet);

			// Send the proxy packet to the realm server
			m_connector.SendProxyPacket(m_character->GetGuid(), packet.GetId(), packet.GetSize(), buffer);
		}

	public:
		/// @copydoc TileSubscriber::GetGameUnit
		const GameUnitS& GetGameUnit() const override { return *m_character; }

		void NotifyObjectsUpdated(const std::vector<GameObjectS*>& objects) const override;

		/// @copydoc TileSubscriber::NotifyObjectsSpawned
		void NotifyObjectsSpawned(const std::vector<GameObjectS*>& object) const override;

		/// @copydoc TileSubscriber::NotifyObjectsDespawned
		void NotifyObjectsDespawned(const std::vector<GameObjectS*>& object) const override;

		/// @copydoc TileSubscriber::SendPacket
		void SendPacket(game::Protocol::OutgoingPacket& packet, const std::vector<char>& buffer) override;

		void HandleProxyPacket(game::client_realm_packet::Type opCode, std::vector<uint8>& buffer);

		void LocalChatMessage(ChatType type, const std::string& message);

	public:
		TileIndex2D GetTileIndex() const;

	private:
		/// @brief Executed when the character spawned on a world instance.
		/// @param instance The world instance that the character spawned in.
		void OnSpawned(WorldInstance& instance);

		void OnTileChangePending(VisibilityTile& oldTile, VisibilityTile& newTile);

		void SpawnTileObjects(VisibilityTile& tile);

	public:
		/// @brief Gets the character guid.
		[[nodiscard]] uint64 GetCharacterGuid() const noexcept { return m_character->GetGuid(); }

	private:
		void OnSetSelection(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnMovement(uint16 opCode, uint32 size, io::Reader& contentReader);

	private:
		RealmConnector& m_connector;
		std::shared_ptr<GameUnitS> m_character;
		WorldInstance* m_worldInstance { nullptr };
		CharacterData m_characterData;
		const proto::Project& m_project;
	};

}
