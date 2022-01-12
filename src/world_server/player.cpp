// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "player.h"

#include "game/each_tile_in_region.h"
#include "game/each_tile_in_sight.h"
#include "game/visibility_tile.h"

namespace mmo
{
	Player::Player(RealmConnector& realmConnector, std::shared_ptr<GameObjectS> characterObject)
		: m_connector(realmConnector)
		, m_character(std::move(characterObject))
	{
		m_character->spawned.connect(*this, &Player::OnSpawned);
		m_character->tileChangePending.connect(*this, &Player::OnTileChangePending);
	}

	Player::~Player()
	{
		if (m_worldInstance && m_character)
		{
			m_worldInstance->RemoveGameObject(*m_character);
		}
	}

	void Player::NotifyObjectsSpawned(std::vector<GameObjectS*> object) const
	{
		VisibilityTile &tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());
		SendPacket([](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::UpdateObject);
			// TODO: Write spawn packet (others)
			outPacket.Finish();
		});
	}

	void Player::NotifyObjectsDespawned(std::vector<GameObjectS*> object) const
	{
		VisibilityTile &tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());
		SendPacket([&tile](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::DestroyObjects);
			outPacket << io::write<uint16>(tile.GetGameObjects().size());
			for (const auto *gameObject : tile.GetGameObjects())
			{
				outPacket << io::write_packed_guid(gameObject->GetGuid());
			}
			outPacket.Finish();
		});
	}

	void Player::SendPacket(game::Protocol::OutgoingPacket& packet, const std::vector<char>& buffer)
	{
		m_connector.SendProxyPacket(m_character->GetGuid(), packet.GetId(), packet.GetSize(), buffer);
	}

	TileIndex2D Player::GetTileIndex() const
	{
		ASSERT(m_worldInstance);

		TileIndex2D position;
		m_worldInstance->GetGrid().GetTilePosition(
			m_character->GetPosition(), position[0], position[1]);

		return position;
	}

	void Player::OnSpawned(WorldInstance& instance)
	{
		m_worldInstance = &instance;
		
		VisibilityTile &tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());
		tile.GetWatchers().add(this);

		SendPacket([](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::UpdateObject);
			// TODO: Self spawn packet
			outPacket.Finish();
		});
	}

	void Player::OnTileChangePending(VisibilityTile& oldTile, VisibilityTile& newTile)
	{
		ASSERT(m_worldInstance);
		
		oldTile.GetWatchers().remove(this);
		newTile.GetWatchers().add(this);
		
		ForEachTileInSightWithout(
			m_worldInstance->GetGrid(),
			oldTile.GetPosition(),
			newTile.GetPosition(),
			[this](VisibilityTile &tile)
			{
				if (tile.GetGameObjects().empty())
				{
					return;
				}

				this->SendPacket([&tile](game::OutgoingPacket& outPacket)
				{
					outPacket.Start(game::realm_client_packet::DestroyObjects);
					outPacket << io::write<uint16>(tile.GetGameObjects().size());
					for (const auto *object : tile.GetGameObjects())
					{
						outPacket << io::write_packed_guid(object->GetGuid());
					}
					outPacket.Finish();
				});
			});

		ForEachTileInSightWithout(
			m_worldInstance->GetGrid(),
			newTile.GetPosition(),
			oldTile.GetPosition(),
			[this](VisibilityTile &tile)
		{
			for (auto *obj : tile.GetGameObjects())
			{
				ASSERT(obj);
				
				std::vector<std::vector<char>> createBlock;
				CreateUpdateBlocks(*obj, createBlock);

				// TODO: Send spawn packets
			}
		});
	}
}
