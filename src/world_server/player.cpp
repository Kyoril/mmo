// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "player.h"

#include "base/utilities.h"
#include "game/each_tile_in_region.h"
#include "game/each_tile_in_sight.h"
#include "game/visibility_tile.h"
#include "game/game_object_s.h"

namespace mmo
{
	Player::Player(RealmConnector& realmConnector, std::shared_ptr<GameUnitS> characterObject)
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
			VisibilityTile &tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());
			tile.GetWatchers().optionalRemove(this);
			m_worldInstance->RemoveGameObject(*m_character);
		}
	}

	void Player::NotifyObjectsSpawned(std::vector<GameObjectS*>& objects) const
	{
		SendPacket([&objects](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::UpdateObject);
			outPacket << io::write<uint16>(objects.size());
			for (const auto& object : objects)
			{
				object->WriteObjectUpdateBlock(outPacket);
			}
			outPacket.Finish();
		});
	}

	void Player::NotifyObjectsDespawned(std::vector<GameObjectS*>& objects) const
	{
		VisibilityTile &tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());
		SendPacket([&objects](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::DestroyObjects);
			outPacket << io::write<uint16>(objects.size());
			for (const auto *gameObject : objects)
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

	void Player::HandleProxyPacket(game::client_realm_packet::Type opCode, std::vector<uint8>& buffer)
	{
		io::MemorySource source(reinterpret_cast<char*>(buffer.data()), reinterpret_cast<char*>(buffer.data() + buffer.size()));
		io::Reader reader(source);

		// TODO: Enum packet opcode
		HandleMovementPacket(opCode, buffer.size(), reader);
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
		
		// Self spawn
		std::vector object(1, (GameObjectS*)m_character.get());
		NotifyObjectsSpawned( object);

		VisibilityTile &tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());
		tile.GetWatchers().add(this);
		
		// Spawn tile objects
		ForEachTileInSight(
			m_worldInstance->GetGrid(),
			tile.GetPosition(),
			[this](VisibilityTile &tile)
		{
			SpawnTileObjects(tile);
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
			SpawnTileObjects(tile);
		});
	}

	void Player::SpawnTileObjects(VisibilityTile& tile)
	{
		std::vector<GameObjectS*> objects;
		objects.reserve(tile.GetGameObjects().size());

		for (auto *obj : tile.GetGameObjects())
		{
			ASSERT(obj);
			if (obj->GetGuid() == GetCharacterGuid())
			{
				continue;
			}
				
			objects.push_back(obj);
		}

		if (objects.empty())
		{
			return;
		}

		NotifyObjectsSpawned(objects);
	}

	void Player::HandleMovementPacket(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 characterGuid;
		MovementInfo info;
		if (!(contentReader >> io::read<uint64>(characterGuid) >> info))
		{
			ELOG("Failed to read movement packet")
			return;
		}

		DLOG("Character moved to location " << info.position << " (facing " << info.facing.GetValueDegrees() << " degrees) with movement flags " << log_hex_digit(info.movementFlags));
		
		VisibilityTile &tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());

		// Translate client-side movement op codes into server side movement op codes for the receiving clients
		switch(opCode)
		{
		case game::client_realm_packet::MoveStartForward: opCode = game::realm_client_packet::MoveStartForward; break;
		case game::client_realm_packet::MoveStartBackward: opCode = game::realm_client_packet::MoveStartBackward; break;
		case game::client_realm_packet::MoveStop: opCode = game::realm_client_packet::MoveStop; break;
		case game::client_realm_packet::MoveStartStrafeLeft: opCode = game::realm_client_packet::MoveStartStrafeLeft; break;
		case game::client_realm_packet::MoveStartStrafeRight: opCode = game::realm_client_packet::MoveStartStrafeRight; break;
		case game::client_realm_packet::MoveStopStrafe: opCode = game::realm_client_packet::MoveStopStrafe; break;
		case game::client_realm_packet::MoveStartTurnLeft: opCode = game::realm_client_packet::MoveStartTurnLeft; break;
		case game::client_realm_packet::MoveStartTurnRight: opCode = game::realm_client_packet::MoveStartTurnRight; break;
		case game::client_realm_packet::MoveStopTurn: opCode = game::realm_client_packet::MoveStopTurn; break;
		case game::client_realm_packet::MoveHeartBeat: opCode = game::realm_client_packet::MoveHeartBeat; break;
		case game::client_realm_packet::MoveSetFacing: opCode = game::realm_client_packet::MoveSetFacing; break;
		default:
			WLOG("Received unknown movement packet " << log_hex_digit(opCode) << ", ensure that it is handled");
		}

		std::vector<char> buffer;
		io::VectorSink sink { buffer };
		game::OutgoingPacket movementPacket { sink };
		movementPacket.Start(opCode);
		movementPacket << io::write<uint64>(characterGuid) << info;
		movementPacket.Finish();

		ForEachTileInSight(
			m_worldInstance->GetGrid(),
			tile.GetPosition(),
			[characterGuid, &buffer, &movementPacket](VisibilityTile &tile)
		{
			for (const auto& watcher : tile.GetWatchers())
			{
				if (watcher->GetGameUnit().GetGuid() == characterGuid)
				{
					continue;
				}

				watcher->SendPacket(movementPacket, buffer);
			}
		});
	}
}
