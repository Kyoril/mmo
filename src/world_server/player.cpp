// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "player.h"

#include "player_manager.h"
#include "base/utilities.h"
#include "game_server/each_tile_in_region.h"
#include "game_server/each_tile_in_sight.h"
#include "game_server/game_creature_s.h"
#include "game_server/visibility_tile.h"
#include "game_server/game_object_s.h"
#include "game_server/game_player_s.h"
#include "game/spell_target_map.h"
#include "game_server/game_bag_s.h"
#include "proto_data/project.h"
#include "game/loot.h"

namespace mmo
{
	Player::Player(PlayerManager& playerManager, RealmConnector& realmConnector, std::shared_ptr<GamePlayerS> characterObject, CharacterData characterData, const proto::Project& project)
		: m_manager(playerManager)
		, m_connector(realmConnector)
		, m_character(std::move(characterObject))
		, m_characterData(std::move(characterData))
		, m_project(project)
	{
		m_character->SetNetUnitWatcher(this);

		// Inventory change signals
		auto& inventory = m_character->GetInventory();

		m_characterConnections += {
			// Spawn signals
			m_character->spawned.connect(*this, &Player::OnSpawned),
			m_character->despawned.connect(*this, &Player::OnDespawned),

			// Movement signals
			m_character->tileChangePending.connect(*this, &Player::OnTileChangePending),

			// Spell signals
			m_character->spellLearned.connect(*this, &Player::OnSpellLearned),
			m_character->spellUnlearned.connect(*this, &Player::OnSpellUnlearned),

			// Inventory signals
			inventory.itemInstanceCreated.connect(this, &Player::OnItemCreated),
			inventory.itemInstanceUpdated.connect(this, &Player::OnItemUpdated),
			inventory.itemInstanceDestroyed.connect(this, &Player::OnItemDestroyed),
		};

		m_character->SetInitialSpells(m_characterData.spellIds);
	}

	Player::~Player()
	{
		if (m_character)
		{
			m_character->SetNetUnitWatcher(nullptr);
		}

		if (m_worldInstance && m_character)
		{
			VisibilityTile &tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());
			tile.GetWatchers().optionalRemove(this);
			m_worldInstance->RemoveGameObject(*m_character);
		}
	}

	void Player::OnItemCreated(std::shared_ptr<GameItemS> item, uint16 slot)
	{
		const std::vector<GameObjectS*> objects{ item.get() };
		NotifyObjectsSpawned(objects);
	}

	void Player::OnItemUpdated(std::shared_ptr<GameItemS> item, uint16 slot)
	{
		const std::vector<GameObjectS*> objects{ item.get() };
		NotifyObjectsUpdated(objects);
	}

	void Player::OnItemDestroyed(std::shared_ptr<GameItemS> item, uint16 slot)
	{
		const std::vector<GameObjectS*> objects{ item.get() };
		NotifyObjectsDespawned(objects);
	}

	void Player::NotifyObjectsUpdated(const std::vector<GameObjectS*>& objects) const
	{
		SendPacket([&objects](game::OutgoingPacket& outPacket)
			{
				outPacket.Start(game::realm_client_packet::UpdateObject);
				outPacket << io::write<uint16>(objects.size());
				for (const auto& object : objects)
				{
					object->WriteObjectUpdateBlock(outPacket, false);
				}
				outPacket.Finish();
			});
	}

	void Player::NotifyObjectsSpawned(const std::vector<GameObjectS*>& objects) const
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

	void Player::NotifyObjectsDespawned(const std::vector<GameObjectS*>& objects) const
	{
		const uint64 currentTarget = m_character->Get<uint64>(object_fields::TargetUnit);
		if (currentTarget != 0)
		{
			for (const auto *gameObject : objects)
			{
				if (gameObject->GetGuid() == currentTarget)
				{
					m_character->Set<uint64>(object_fields::TargetUnit, 0);
					break;
				}
			}
		}

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

		switch(opCode)
		{
		case game::client_realm_packet::SetSelection:
			OnSetSelection(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::CheatCreateMonster:
			OnCheatCreateMonster(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::CheatDestroyMonster:
			OnCheatDestroyMonster(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::CheatFaceMe:
			OnCheatFaceMe(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::CheatFollowMe:
			OnCheatFollowMe(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::CheatLearnSpell:
			OnCheatLearnSpell(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::CastSpell:
			OnSpellCast(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::AttackSwing:
			OnAttackSwing(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::AttackStop:
			OnAttackStop(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::ReviveRequest:
			OnReviveRequest(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::AutoEquipItem:
			OnAutoEquipItem(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::AutoStoreBagItem:
			OnAutoStoreBagItem(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::SwapItem:
			OnSwapItem(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::SwapInvItem:
			OnSwapInvItem(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::AutoEquipItemSlot:
			OnAutoEquipItemSlot(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::DestroyItem:
			OnDestroyItem(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::Loot:
			OnLoot(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::AutoStoreLootItem:
			OnAutoStoreLootItem(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::LootMoney:
			OnLootMoney(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::LootRelease:
			OnLootRelease(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::MoveStartForward:
		case game::client_realm_packet::MoveStartBackward:
		case game::client_realm_packet::MoveStop:
		case game::client_realm_packet::MoveStartStrafeLeft:
		case game::client_realm_packet::MoveStartStrafeRight:
		case game::client_realm_packet::MoveStopStrafe:
		case game::client_realm_packet::MoveStartTurnLeft:
		case game::client_realm_packet::MoveStartTurnRight:
		case game::client_realm_packet::MoveStopTurn:
		case game::client_realm_packet::MoveHeartBeat:
		case game::client_realm_packet::MoveSetFacing:
		case game::client_realm_packet::MoveJump:
		case game::client_realm_packet::MoveFallLand:
			OnMovement(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::ForceMoveSetWalkSpeedAck:
		case game::client_realm_packet::ForceMoveSetRunSpeedAck:
		case game::client_realm_packet::ForceMoveSetRunBackSpeedAck:
		case game::client_realm_packet::ForceMoveSetSwimSpeedAck:
		case game::client_realm_packet::ForceMoveSetSwimBackSpeedAck:
		case game::client_realm_packet::ForceMoveSetTurnRateAck:
		case game::client_realm_packet::ForceSetFlightSpeedAck:
		case game::client_realm_packet::ForceSetFlightBackSpeedAck:
		case game::client_realm_packet::MoveTeleportAck:
			OnClientAck(opCode, buffer.size(), reader);
			break;
		}
	}

	void Player::LocalChatMessage(ChatType type, const std::string& message)
	{
		VisibilityTile& tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());

		auto position = m_character->GetPosition();
		float chatDistance = 0.0f;
		switch (type)
		{
		case ChatType::Say:
			chatDistance = 25.0f;
			break;
		case ChatType::Yell:
			chatDistance = 300.0f;
			break;
		case ChatType::Emote:
			chatDistance = 50.0f;
			break;
		default: 
			return;
		}

		// TODO: Flags
		constexpr uint8 flags = 0;

		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		game::OutgoingPacket outPacket(sink);
		outPacket.Start(game::realm_client_packet::ChatMessage);
		outPacket
			<< io::write_packed_guid(m_character->GetGuid())
			<< io::write<uint8>(type)
			<< io::write_range(message)
			<< io::write<uint8>(0)
			<< io::write<uint8>(flags);
		outPacket.Finish();

		// Spawn tile objects
		ForEachSubscriberInSight(
			m_worldInstance->GetGrid(),
			tile.GetPosition(),
			[&position, chatDistance, &outPacket, &buffer](TileSubscriber& subscriber)
			{
				auto& unit = subscriber.GetGameUnit();
				const float distanceSquared = (unit.GetPosition() - position).GetSquaredLength();
				if (distanceSquared > chatDistance * chatDistance)
				{
					return;
				}

				subscriber.SendPacket(outPacket, buffer);
			});
	}

	bool Player::IsLooting() const
	{
		return m_loot != nullptr;
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
		std::vector<GameObjectS*> objects;

		// Ensure the inventory is initialized
		m_character->GetInventory().ConstructFromRealmData(objects);
		objects.push_back(m_character.get());

		// Notify player about spawned objects
		NotifyObjectsSpawned(objects);

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

		// Send initial spells
		SendPacket([&](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::InitialSpells);
			packet << io::write_dynamic_range<uint16>(m_characterData.spellIds);
			packet.Finish();
		});

		// Start regeneration immediately
		m_character->StartRegeneration();
	}

	void Player::OnDespawned(GameObjectS& object)
	{
		SaveCharacterData();
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

	void Player::SaveCharacterData() const
	{
		if (!m_character)
		{
			return;
		}

		m_connector.SendCharacterData(*m_character);
	}

	void Player::OpenLootDialog(std::shared_ptr<LootInstance> lootInstance, std::shared_ptr<GameObjectS> source)
	{
		// First, close a potential previous loot dialog
		CloseLootDialog();

		if (!source)
		{
			WLOG("No loot source given!");
			return;
		}

		// Check if the distance is okay
		if (source->HasMovementInfo() &&
			m_character->GetSquaredDistanceTo(source->GetPosition(), true) >= LootDistance * LootDistance)
		{
			// Distance is too big
			WLOG("Player tried to open loot of target which is too far away!");
			return;
		}

		m_loot = lootInstance;
		m_lootSource = source;

		m_character->AddFlag<uint32>(object_fields::Flags, unit_flags::Looting);
		m_character->CancelCast(spell_interrupt_flags::Any);

		// Watch loot source
		m_onLootSourceDespawned = source->despawned.connect([this](GameObjectS& object)
		{
			CloseLootDialog();
		});

		// Watch for loot signals
		m_lootSignals += {
			m_loot->cleared.connect([this]()
			{
				CloseLootDialog();
			}),
			m_loot->itemRemoved.connect([this](uint8 slot)
			{
				SendPacket([&slot](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::LootRemoved);
					packet << io::write<uint8>(slot);
					packet.Finish();
				});
			}),
		};

		// Send the actual loot data
		SendPacket([&](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::LootResponse);
			packet
				<< io::write<uint64>(m_loot->getLootGuid())
				<< io::write<uint8>(loot_type::Corpse);
			lootInstance->Serialize(packet, m_character->GetGuid());
			packet.Finish();
		});
	}

	void Player::CloseLootDialog()
	{
		m_lootSignals.disconnect();
		m_onLootSourceDespawned.disconnect();

		if (!m_loot)
		{
			return;
		}

		m_loot->closed(m_character->GetGuid());

		// Notify player
		SendPacket([&](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::LootReleaseResponse);
			packet << io::write<uint64>(m_loot->getLootGuid());
			packet.Finish();
		});

		m_character->RemoveFlag<uint32>(object_fields::Flags, unit_flags::Looting);

		m_loot = nullptr;
		m_lootSource = nullptr;
	}

	void Player::OnSetSelection(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 selectedObject;
		if (!(contentReader >> io::read<uint64>(selectedObject)))
		{
			return;
		}

		// Update field (update will be sent to all clients around)
		m_character->SetTarget(selectedObject);
	}

	void Player::Kick()
	{
		m_worldInstance->RemoveGameObject(*m_character);
		m_character.reset();
	}

	void Player::OnMovement(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 characterGuid;
		MovementInfo info;
		if (!(contentReader >> io::read<uint64>(characterGuid) >> info))
		{
			ELOG("Failed to read movement packet")
			return;
		}

		if (characterGuid != m_character->GetGuid())
		{
			ELOG("User is trying to move a different character than himself")
			return;
		}

		if ((info.IsStrafing() || info.IsMoving() || info.IsTurning() || info.IsPitching()) && !m_character->IsAlive())
		{
			ELOG("Player tried to move or rotate while not being alive anymore");
			return;
		}

		// Make sure that there is no timed out pending movement change (lag tolerance)
		if (m_character->HasTimedOutPendingMovementChange())
		{
			ELOG("Player probably tried to skip or delay an ack packet");
			Kick();
			return;
		}

		// Did the client try to sneak in a FALLING flag without sending a jump packet?
		if (info.IsFalling() && !m_character->GetMovementInfo().IsFalling() && opCode != game::client_realm_packet::MoveJump)
		{
			ELOG("Client tried to apply FALLING flag in non-jump packet!");
			Kick();
			return;
		}
		// Did the client try to remove a FALLING flag without sending a landing packet?
		if (!info.IsFalling() && m_character->GetMovementInfo().IsFalling() && opCode != game::client_realm_packet::MoveFallLand)
		{
			ELOG("Client tried to apply FALLING flag in non-jump packet!");
			Kick();
			return;
		}

		if (opCode == game::client_realm_packet::MoveJump)
		{
			if (m_character->GetMovementInfo().IsFalling() || !info.IsFalling())
			{
				ELOG("Jump packet did not add FALLING movement flag or was executed while already falling");
				Kick();
				return;
			}
		}
		if (opCode == game::client_realm_packet::MoveFallLand)
		{
			if (!m_character->GetMovementInfo().IsFalling() || info.IsFalling())
			{
				ELOG("Landing packet did not remove FALLING movement flag or was executed while not falling")
				Kick();
				return;
			}
		}

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
		case game::client_realm_packet::MoveJump: opCode = game::realm_client_packet::MoveJump; break;
		case game::client_realm_packet::MoveFallLand: opCode = game::realm_client_packet::MoveFallLand; break;
		default:
			WLOG("Received unknown movement packet " << log_hex_digit(opCode) << ", ensure that it is handled");
		}
		
		if (!m_character->GetMovementInfo().IsChangingPosition())
		{
			if (info.position != m_character->GetPosition())
			{
				ELOG("User position changed on client while it should not be able to do so based on server info")
				return;
			}
		}

		if (opCode == game::realm_client_packet::MoveStartForward)
		{
			if (m_character->GetMovementInfo().movementFlags & movement_flags::Forward)
			{
				ELOG("User starts moving forward but was already moving forward")
				return;
			}
		}
		else if (opCode == game::realm_client_packet::MoveStartBackward)
		{
			if (m_character->GetMovementInfo().movementFlags & movement_flags::Backward)
			{
				ELOG("User starts moving backward but was already moving backward")
				return;
			}
		}
		else if (opCode == game::realm_client_packet::MoveStop)
		{
			if ((m_character->GetMovementInfo().movementFlags & movement_flags::Moving) == 0)
			{
				ELOG("User stops movement but was not moving")
				return;
			}
		}

		m_character->ApplyMovementInfo(info);

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

	void Player::OnCheatCreateMonster(uint16 opCode, uint32 size, io::Reader& contentReader) const
	{
		uint32 entry;
		if (!(contentReader >> io::read<uint32>(entry)))
		{
			ELOG("Missing entry id to create a monster");
			return;
		}

		DLOG("Creating monster with entry " << entry);

		const auto* creatureEntry = m_project.units.getById(entry);

		// Spawn a new creature
		ASSERT(m_worldInstance);
		const auto spawned = m_worldInstance->CreateTemporaryCreature(*creatureEntry, m_character->GetPosition(), 0.0f, 50.0f);
		spawned->ClearFieldChanges();
		m_worldInstance->AddGameObject(*spawned);
	}

	void Player::OnCheatDestroyMonster(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 guid;
		if (!(contentReader >> io::read<uint64>(guid)))
		{
			ELOG("Missing guid to destroy a monster");
			return;
		}

		DLOG("Destroying monster with guid " << log_hex_digit(guid));

		// Find creature with guid
		GameObjectS* object = m_worldInstance->FindObjectByGuid(guid);
		if (object == nullptr)
		{
			ELOG("Unable to find object with guid " << log_hex_digit(guid) << " to destroy");
			return;
		}

		if (object->GetTypeId() != ObjectTypeId::Unit)
		{
			ELOG("Object with guid " << log_hex_digit(guid) << " is not a creature");
			return;
		}

		m_worldInstance->RemoveGameObject(*object);
	}

	void Player::OnCheatLearnSpell(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 spellId;
		if (!(contentReader >> io::read<uint32>(spellId)))
		{
			ELOG("Missing spell id to learn a spell");
			return;
		}

		// Find spell with entry
		const auto* spell = m_project.spells.getById(spellId);
		if (!spell)
		{
			ELOG("Unable to learn spell: Unknown spell " << spellId);
			return;
		}

		DLOG("Learning spell " << spellId << " (" << spell->name() << " [" << spell->rank() << "])");

		// Check if we have a player character in target
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		// Find target unit
		GameObjectS* targetObject = m_worldInstance->FindObjectByGuid(targetGuid);
		if (!targetObject || targetObject->GetTypeId() != ObjectTypeId::Player)
		{
			targetObject = m_character.get();
		}

		auto playerCharacter = reinterpret_cast<GamePlayerS*>(targetObject);
		playerCharacter->AddSpell(spellId);
	}

	void Player::OnSpellCast(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		// Read spell cast packet
		uint32 spellId;
		SpellTargetMap targetMap;

		if (!(contentReader >> io::read<uint32>(spellId) >> targetMap))
		{
			WLOG("Could not read packet data");
			return;
		}

		// Look for the spell
		const auto* spell = m_project.spells.getById(spellId);
		if (!spell)
		{
			ELOG("Tried to cast unknown spell " << spellId);
			return;
		}

		// Get the cast time of this spell
		int64 castTime = spell->casttime();

		// TODO: Apply cast time modifiers

		const uint64 casterId = m_character->GetGuid();

		// Spell cast logic
		auto result = m_character->CastSpell(targetMap, *spell, castTime);
		if (result != spell_cast_result::CastOkay)
		{
			SendPacket([&result, spellId, casterId](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::SpellFailure);
				packet
					<< io::write_packed_guid(casterId)
					<< io::write<uint32>(spellId)
					<< io::write<GameTime>(GetAsyncTimeMs())
					<< io::write<uint8>(result);
				packet.Finish();
			});
		}
	}

	void Player::OnAttackSwing(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 victimGuid;
		GameTime clientTimestamp;
		if (!(contentReader >> io::read_packed_guid(victimGuid) >> io::read<GameTime>(clientTimestamp)))
		{
			ELOG("Failed to read victim guid and client timestamp for attack swing");
			return;
		}

		// Cant attack ourself
		if (victimGuid == m_character->GetGuid())
		{
			return;
		}

		GameObjectS* targetObject = m_worldInstance->FindObjectByGuid(victimGuid);
		if (!targetObject)
		{
			ELOG("Failed to find target object with guid " << log_hex_digit(victimGuid));
			return;
		}

		if (targetObject->GetTypeId() != ObjectTypeId::Unit && targetObject->GetTypeId() != ObjectTypeId::Player)
		{
			ELOG("Target object with guid " << log_hex_digit(victimGuid) << " is not a unit and thus not attackable");
			return;
		}

		m_character->StartAttack(std::static_pointer_cast<GameUnitS>(targetObject->shared_from_this()));
	}

	void Player::OnAttackStop(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		GameTime clientTimestamp;
		if (!(contentReader >> io::read<GameTime>(clientTimestamp)))
		{
			ELOG("Failed to read client timestamp for attack stop");
			return;
		}
	}

	void Player::OnCheatFollowMe(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 guid;
		if (!(contentReader >> io::read<uint64>(guid)))
		{
			ELOG("Missing guid");
			return;
		}

		DLOG("Making Monster with guid " << log_hex_digit(guid) << " follow player");

		// Find creature with guid
		GameObjectS* object = m_worldInstance->FindObjectByGuid(guid);
		if (object == nullptr)
		{
			ELOG("Unable to find object with guid " << log_hex_digit(guid));
			return;
		}

		if (object->GetTypeId() != ObjectTypeId::Unit)
		{
			ELOG("Object with guid " << log_hex_digit(guid) << " is not a creature");
			return;
		}

		// Stop movement immediately
		GameUnitS* unit = reinterpret_cast<GameUnitS*>(object);
		unit->GetMover().StopMovement();

		// TODO
		DLOG("TODO");
	}

	void Player::OnCheatFaceMe(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 guid;
		if (!(contentReader >> io::read<uint64>(guid)))
		{
			ELOG("Missing guid");
			return;
		}

		DLOG("Making Monster with guid " << log_hex_digit(guid) << " face player");

		// Find creature with guid
		GameObjectS* object = m_worldInstance->FindObjectByGuid(guid);
		if (object == nullptr)
		{
			ELOG("Unable to find object with guid " << log_hex_digit(guid));
			return;
		}

		if (object->GetTypeId() != ObjectTypeId::Unit)
		{
			ELOG("Object with guid " << log_hex_digit(guid) << " is not a creature");
			return;
		}

		// Stop movement immediately
		GameUnitS* unit = reinterpret_cast<GameUnitS*>(object);
		unit->GetMover().StopMovement();

		// TODO
		DLOG("TODO");

		
	}

	void Player::OnReviveRequest(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		// Check if player is actually dead
		if (m_character->IsAlive())
		{
			ELOG("Player tried to revive while being alive");
			return;
		}

		m_character->TeleportOnMap(m_character->GetBindPosition(), m_character->GetBindFacing());

		// For now, we simply reset the player health back to the maximum health value.
		// We will need to teleport the player back to it's binding point once teleportation is supported!
		m_character->Set<uint32>(object_fields::Health, m_character->Get<uint32>(object_fields::MaxHealth) / 2);
		if (m_character->Get<uint32>(object_fields::MaxMana) > 1)
		{
			m_character->Set<uint32>(object_fields::Mana, m_character->Get<uint32>(object_fields::MaxMana) / 2);
		}

		m_character->StartRegeneration();
	}

	bool ValidateSpeedAck(const PendingMovementChange& change, float receivedSpeed, MovementType& outMoveTypeSent)
	{
		switch (change.changeType)
		{
		case MovementChangeType::SpeedChangeWalk:				outMoveTypeSent = movement_type::Walk; break;
		case MovementChangeType::SpeedChangeRun:				outMoveTypeSent = movement_type::Run; break;
		case MovementChangeType::SpeedChangeRunBack:			outMoveTypeSent = movement_type::Backwards; break;
		case MovementChangeType::SpeedChangeSwim:				outMoveTypeSent = movement_type::Swim; break;
		case MovementChangeType::SpeedChangeSwimBack:			outMoveTypeSent = movement_type::SwimBackwards; break;
		case MovementChangeType::SpeedChangeTurnRate:			outMoveTypeSent = movement_type::Turn; break;
		case MovementChangeType::SpeedChangeFlightSpeed:		outMoveTypeSent = movement_type::Flight; break;
		case MovementChangeType::SpeedChangeFlightBackSpeed:	outMoveTypeSent = movement_type::FlightBackwards; break;
		default:
			ELOG("Incorrect ack data for speed change ack");
			return false;
		}

		if (std::fabs(receivedSpeed - change.speed) > FLT_EPSILON)
		{
			ELOG("Incorrect speed value received in ack");
			return false;
		}

		return true;
	}

	void Player::OnClientAck(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 ackId;
		if (!(contentReader >> io::read<uint32>(ackId)))
		{
			ELOG("Failed to read ack id");
			Kick();
			return;
		}

		if (!m_character->HasPendingMovementChange())
		{
			ELOG("Received ack for movement change but no pending movement change was found");
			Kick();
			return;
		}

		// Try to consume client ack
		PendingMovementChange change = m_character->PopPendingMovementChange();
		if (change.counter != ackId)
		{
			ELOG("Received client ack with wrong index (different index expected)");
			Kick();
			return;
		}

		// Read movement info if applicable
		MovementInfo info;
		if (!(contentReader >> info))
		{
			ELOG("Could not read movement info from ack packet 0x" << std::hex << opCode);
			return;
		}
		
		// TODO: Validate movement speed


		// Used by speed change acks
		MovementType typeSent = movement_type::Count;
		float receivedSpeed = 0.0f;

		// Check op-code dependant checks and actions
		switch (opCode)
		{
		case game::client_realm_packet::ForceMoveSetWalkSpeedAck:
		case game::client_realm_packet::ForceMoveSetRunSpeedAck:
		case game::client_realm_packet::ForceMoveSetRunBackSpeedAck:
		case game::client_realm_packet::ForceMoveSetSwimSpeedAck:
		case game::client_realm_packet::ForceMoveSetSwimBackSpeedAck:
		case game::client_realm_packet::ForceMoveSetTurnRateAck:
		case game::client_realm_packet::ForceSetFlightSpeedAck:
		case game::client_realm_packet::ForceSetFlightBackSpeedAck:
			{
				// Read the additional new speed value (note that this is in units/second).
				if (!(contentReader >> receivedSpeed))
				{
					WLOG("Incomplete ack packet data received!");
					Kick();
					return;
				}

				// Validate that all parameters match the pending movement change and also determine
				// the movement type that should be altered based on the change.
				if (!ValidateSpeedAck(change, receivedSpeed, typeSent))
				{
					Kick();
					return;
				}

				// Used to validate if the opCode matches the determined move code
				static uint16 speedAckOpCodes[MovementType::Count] = {
					game::client_realm_packet::ForceMoveSetWalkSpeedAck,
					game::client_realm_packet::ForceMoveSetRunSpeedAck,
					game::client_realm_packet::ForceMoveSetRunBackSpeedAck,
					game::client_realm_packet::ForceMoveSetSwimSpeedAck,
					game::client_realm_packet::ForceMoveSetSwimBackSpeedAck,
					game::client_realm_packet::ForceMoveSetTurnRateAck,
					game::client_realm_packet::ForceSetFlightSpeedAck,
					game::client_realm_packet::ForceSetFlightBackSpeedAck
				};

				// Validate that the movement type has the expected value based on the opcode
				if (typeSent >= MovementType::Count ||
					opCode != speedAckOpCodes[typeSent])
				{
					WLOG("Wrong movement type in speed ack packet!");
					Kick();
					return;
				}

				// Determine the base speed and make sure it's greater than 0, since we want to
				// use it as divider
				const float baseSpeed = m_character->GetBaseSpeed(typeSent);
				ASSERT(baseSpeed > 0.0f);

				// Calculate the speed rate
				receivedSpeed /= baseSpeed;
				break;
			}

		case game::client_realm_packet::MoveTeleportAck:
			if (change.changeType != MovementChangeType::Teleport)
			{
				WLOG("Received wrong ack op-code for expected ack!");
				Kick();
				return;
			}

			DLOG("Received teleport ack towards " << change.teleportInfo.x << "," << change.teleportInfo.y << "," << change.teleportInfo.z);
			break;
		}

		// Apply movement info
		//m_character->ApplyMovementInfo(info);
		m_character->Relocate(info.position, info.facing);

		// Perform application - we need to do this after all checks have been made
		switch (opCode)
		{
		case game::client_realm_packet::ForceMoveSetWalkSpeedAck:
		case game::client_realm_packet::ForceMoveSetRunSpeedAck:
		case game::client_realm_packet::ForceMoveSetRunBackSpeedAck:
		case game::client_realm_packet::ForceMoveSetSwimSpeedAck:
		case game::client_realm_packet::ForceMoveSetSwimBackSpeedAck:
		case game::client_realm_packet::ForceMoveSetTurnRateAck:
		case game::client_realm_packet::ForceSetFlightSpeedAck:
		case game::client_realm_packet::ForceSetFlightBackSpeedAck:
			// Apply speed rate so that anti cheat detection can detect speed hacks
			// properly now since we made sure that the client has received the speed
			// change and all following movement packets need to use these new speed
			// values.
			m_character->ApplySpeedChange(typeSent, receivedSpeed);
			break;
		}
	}

	void Player::OnAutoStoreLootItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 lootSlot;
		if (!(contentReader >> io::read<uint8>(lootSlot)))
		{
			WLOG("Failed to read loot slot");
			return;
		}

		// TODO: Check current loot
		if (!m_loot)
		{
			WLOG("Player is not looting anything right now!");
			return;
		}

		// Check if the distance is okay
		if (m_lootSource &&
			m_lootSource->HasMovementInfo() &&
			m_character->GetSquaredDistanceTo(m_lootSource->GetPosition(), true) >= LootDistance * LootDistance)
		{
			// Distance is too big
			WLOG("Player tried to open loot of target which is too far away!");
			return;
		}

		const LootItem* lootItem = m_loot->GetLootDefinition(lootSlot);
		if (!lootItem)
		{
			WLOG("Loot slot is empty!");
			return;
		}

		// Already looted?
		if (lootItem->isLooted)
		{
			WLOG("Loot slot is already looted!");
			return;
		}

		const proto::ItemEntry* item = m_project.items.getById(lootItem->definition.item());
		if (!item)
		{
			WLOG("Unable to find item which was generated by loot definition! Game data might be corrupt...");
			return;
		}

		Inventory& inventory = m_character->GetInventory();

		std::map<uint16, uint16> addedBySlot;
		auto result = inventory.CreateItems(*item, lootItem->count, &addedBySlot);
		if (result != inventory_change_failure::Okay)
		{
			ELOG("Failed to add item to inventory: " << result);
			return;
		}

		for (auto& slot : addedBySlot)
		{
			auto inst = inventory.GetItemAtSlot(slot.first);
			if (inst)
			{
				uint8 bag = 0, subslot = 0;
				Inventory::GetRelativeSlots(slot.first, bag, subslot);
				const auto totalCount = inventory.GetItemCount(item->id());

				//sendProxyPacket(
				//	std::bind(server_write::itemPushResult, std::placeholders::_1,
				//		m_character->getGuid(), std::cref(*inst), true, false, bag, subslot, slot.second, totalCount));
			}
		}

		// Consume this item
		auto playerGuid = m_character->GetGuid();
		m_loot->TakeItem(lootSlot, playerGuid);
	}

	void Player::OnAutoEquipItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 srcBag, srcSlot;
		if (!(contentReader >> io::read<uint8>(srcBag) >> io::read<uint8>(srcSlot)))
		{
			WLOG("Failed to read source bag and slot");
			return;
		}

		auto& inv = m_character->GetInventory();
		auto absSrcSlot = Inventory::GetAbsoluteSlot(srcBag, srcSlot);
		auto item = inv.GetItemAtSlot(absSrcSlot);
		if (!item)
		{
			ELOG("Item not found");
			return;
		}

		uint8 targetSlot = 0xFF;

		// Check if item is equippable
		const auto& entry = item->GetEntry();
		switch (entry.inventorytype())
		{
		case inventory_type::Head:
			targetSlot = player_equipment_slots::Head;
			break;
		case inventory_type::Cloak:
			targetSlot = player_equipment_slots::Back;
			break;
		case inventory_type::Neck:
			targetSlot = player_equipment_slots::Neck;
			break;
		case inventory_type::Feet:
			targetSlot = player_equipment_slots::Feet;
			break;
		case inventory_type::Body:
			targetSlot = player_equipment_slots::Body;
			break;
		case inventory_type::Chest:
		case inventory_type::Robe:
			targetSlot = player_equipment_slots::Chest;
			break;
		case inventory_type::Legs:
			targetSlot = player_equipment_slots::Legs;
			break;
		case inventory_type::Shoulders:
			targetSlot = player_equipment_slots::Shoulders;
			break;
		case inventory_type::TwoHandedWeapon:
		case inventory_type::MainHandWeapon:
			targetSlot = player_equipment_slots::Mainhand;
			break;
		case inventory_type::OffHandWeapon:
		case inventory_type::Shield:
		case inventory_type::Holdable:
			targetSlot = player_equipment_slots::Offhand;
			break;
		case inventory_type::Weapon:
			targetSlot = player_equipment_slots::Mainhand;
			break;
		case inventory_type::Finger:
			targetSlot = player_equipment_slots::Finger1;
			break;
		case inventory_type::Trinket:
			targetSlot = player_equipment_slots::Trinket1;
			break;
		case inventory_type::Wrists:
			targetSlot = player_equipment_slots::Wrists;
			break;
		case inventory_type::Tabard:
			targetSlot = player_equipment_slots::Tabard;
			break;
		case inventory_type::Hands:
			targetSlot = player_equipment_slots::Hands;
			break;
		case inventory_type::Waist:
			targetSlot = player_equipment_slots::Waist;
			break;
		case inventory_type::Ranged:
		case inventory_type::RangedRight:
		case inventory_type::Thrown:
			targetSlot = player_equipment_slots::Ranged;
			break;
		default:
			if (entry.itemclass() == item_class::Container ||
				entry.itemclass() == item_class::Quiver)
			{
				for (uint16 slot = player_inventory_slots::Start; slot < player_inventory_slots::End; ++slot)
				{
					auto bag = inv.GetBagAtSlot(slot | 0xFF00);
					if (!bag)
					{
						targetSlot = slot;
						break;
					}
				}

				if (targetSlot == 0xFF)
				{
					//m_character->inventoryChangeFailure(game::inventory_change_failure::NoEquipmentSlotAvailable, item.get(), nullptr);
					return;
				}
			}
			break;
		}

		// Check if valid slot found
		auto absDstSlot = Inventory::GetAbsoluteSlot(player_inventory_slots::Bag_0, targetSlot);
		if (!Inventory::IsEquipmentSlot(absDstSlot) && !Inventory::IsBagPackSlot(absDstSlot))
		{
			ELOG("Invalid target slot: " << targetSlot);
			//m_character->inventoryChangeFailure(game::inventory_change_failure::ItemCantBeEquipped, item.get(), nullptr);
			return;
		}

		// Get item at target slot
		if (auto result = inv.SwapItems(absSrcSlot, absDstSlot); result != inventory_change_failure::Okay)
		{
			// Something went wrong
			ELOG("ERROR: " << result);
		}
	}

	void Player::OnAutoStoreBagItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 srcBag, srcSlot, dstBag;
		if (!(contentReader >> io::read<uint8>(srcBag) >> io::read<uint8>(srcSlot) >> io::read<uint8>(dstBag)))
		{
			WLOG("Failed to read source bag, source slot and destination bag");
			return;
		}

		// TODO
	}

	void Player::OnSwapItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 srcBag, srcSlot, dstBag, dstSlot;
		if (!(contentReader >> io::read<uint8>(srcBag) >> io::read<uint8>(srcSlot) >> io::read<uint8>(dstBag) >> io::read<uint8>(dstSlot)))
		{
			WLOG("Failed to read source bag, source slot, destination bag and destination slot");
			return;
		}
		
		auto& inv = m_character->GetInventory();
		auto result = inv.SwapItems(
			Inventory::GetAbsoluteSlot(srcBag, srcSlot),
			Inventory::GetAbsoluteSlot(dstBag, dstSlot));
		if (result)
		{
			ELOG("ERRROR: " << result);
		}
	}

	void Player::OnSwapInvItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 srcSlot, dstSlot;
		if (!(contentReader >> io::read<uint8>(srcSlot) >> io::read<uint8>(dstSlot)))
		{
			WLOG("Failed to read source slot and destination slot");
			return;
		}

		auto& inv = m_character->GetInventory();
		auto result = inv.SwapItems(
			Inventory::GetAbsoluteSlot(player_inventory_slots::Bag_0, srcSlot),
			Inventory::GetAbsoluteSlot(player_inventory_slots::Bag_0, dstSlot));
		if (result)
		{
			ELOG("ERRROR: " << result);
		}
	}

	void Player::OnSplitItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 srcBag, srcSlot, dstBag, dstSlot, count;
		if (!(contentReader >> io::read<uint8>(srcBag) >> io::read<uint8>(srcSlot) >> io::read<uint8>(dstBag) >> io::read<uint8>(dstSlot) >> io::read<uint8>(count)))
		{
			WLOG("Failed to read source bag, source slot, destination bag, destination slot and count");
			return;
		}

		// TODO
	}

	void Player::OnAutoEquipItemSlot(uint16 opCode, uint32 size, io::Reader& contentReader)
	{

	}

	void Player::OnDestroyItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 bag, slot, count;
		if (!(contentReader >> io::read<uint8>(bag) >> io::read<uint8>(slot) >> io::read<uint8>(count)))
		{
			WLOG("Failed to read bag, slot and count");
			return;
		}

		auto result = m_character->GetInventory().RemoveItem(Inventory::GetAbsoluteSlot(bag, slot), count);
		if (!result)
		{
			// TODO:
			ELOG("ERRROR: " << result);
		}
	}

	void Player::OnLoot(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 objectGuid;
		if (!(contentReader >> io::read<uint64>(objectGuid)))
		{
			WLOG("Failed to read object guid");
			return;
		}

		// Find game object
		GameObjectS* lootObject = m_character->GetWorldInstance()->FindObjectByGuid(objectGuid);
		if (!lootObject)
		{
			ELOG("Player tried to loot non existing object!");
			return;
		}

		if (lootObject->GetTypeId() != ObjectTypeId::Unit)
		{
			SendPacket([objectGuid](game::OutgoingPacket& packet)
				{
					packet.Start(game::realm_client_packet::LootResponse);
					packet
						<< io::write<uint64>(objectGuid)
						<< io::write<uint8>(loot_type::None)
						<< io::write<uint8>(loot_error::Locked);
					packet.Finish();
				});
			return;
		}

		GameCreatureS* creature = reinterpret_cast<GameCreatureS*>(lootObject);

		if (auto loot = creature->getUnitLoot())
		{
			OpenLootDialog(loot, creature->shared_from_this());
		}
		else
		{
			WLOG("Creature " << log_hex_digit(objectGuid) << " has no loot!");

			SendPacket([objectGuid](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::LootResponse);
				packet
					<< io::write<uint64>(objectGuid)
					<< io::write<uint8>(loot_type::None)
					<< io::write<uint8>(loot_error::Locked);
				packet.Finish();
			});
		}
	}

	void Player::OnLootMoney(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		if (!m_loot)
		{
			ELOG("Player tried to loot money without having a loot window open");
			return;
		}

		// Check if the distance is okay
		if (m_lootSource &&
			m_lootSource->HasMovementInfo() &&
			m_character->GetSquaredDistanceTo(m_lootSource->GetPosition(), true) >= LootDistance * LootDistance)
		{
			// Distance is too big
			WLOG("Player tried to open loot of target which is too far away!");
			return;
		}

		uint32 lootGold = m_loot->getGold();
		if (lootGold == 0)
		{
			WLOG("No gold to loot!");
			return;
		}

		// Check if it's a creature
		std::vector<std::shared_ptr<GamePlayerS>> recipients;
		if (m_lootSource->GetTypeId() == ObjectTypeId::Unit)
		{
			// If looting a creature, loot has to be shared between nearby group members
			std::shared_ptr<GameCreatureS> creature = std::dynamic_pointer_cast<GameCreatureS>(m_lootSource);
			creature->ForEachLootRecipient([&recipients](std::shared_ptr<GamePlayerS>& recipient)
			{
				recipients.push_back(recipient);
			});

			// If this fires, the creature has no loot recipients added. Please check CreatureAIDeathState::OnEnter!
			ASSERT(!recipients.empty());

			// Share gold
			lootGold /= recipients.size();
			if (lootGold == 0)
			{
				lootGold = 1;
			}
		}
		else
		{
			// We will be the only recipient
			recipients.push_back(m_character);
		}

		// Reward with gold
		for (const std::shared_ptr<GamePlayerS>& recipient : recipients)
		{
			uint32 coinage = recipient->Get<uint32>(object_fields::Money);
			if (coinage >= std::numeric_limits<uint32>::max() - lootGold)
			{
				coinage = std::numeric_limits<uint32>::max();
			}
			else
			{
				coinage += lootGold;
			}
			recipient->Set<uint32>(object_fields::Money, coinage);

			// Notify players
			if (std::shared_ptr<Player> player = m_manager.GetPlayerByCharacterGuid(recipient->GetGuid()))
			{
				if (recipients.size() > 1)
				{
					player->SendPacket([lootGold](game::OutgoingPacket& packet)
					{
						packet.Start(game::realm_client_packet::LootMoneyNotify);
						packet << io::write<uint32>(lootGold);
						packet.Finish();
					});
				}

				// TODO: Put this packet into the LootInstance class or in an event callback maybe
				if (m_lootSource &&
					m_lootSource->GetGuid() == m_loot->getLootGuid())
				{
					player->SendPacket([lootGold](game::OutgoingPacket& packet)
					{
						packet.Start(game::realm_client_packet::LootClearMoney);
						packet.Finish();
					});
				}
			}
		}

		// Take gold (WARNING: May reset m_loot as loot may become empty after this)
		m_loot->TakeGold();
	}

	void Player::OnLootRelease(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 objectGuid;
		if (!(contentReader >> io::read<uint64>(objectGuid)))
		{
			WLOG("Failed to read object guid");
			return;
		}

		if (m_lootSource &&
			m_lootSource->GetGuid() != objectGuid)
		{
			WLOG("Player tried to close loot dialog which he didn't open!")
			return;
		}

		CloseLootDialog();
	}

	void Player::OnSpellLearned(GameUnitS& unit, const proto::SpellEntry& spellEntry)
	{
		SendPacket([&spellEntry](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::LearnedSpell);
			packet << io::write<uint32>(spellEntry.id());
			packet.Finish();
		});
	}

	void Player::OnSpellUnlearned(GameUnitS& unit, const proto::SpellEntry& spellEntry)
	{
		SendPacket([&spellEntry](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::UnlearnedSpell);
			packet << io::write<uint32>(spellEntry.id());
			packet.Finish();
		});
	}

	void Player::OnAttackSwingEvent(AttackSwingEvent attackSwingEvent)
	{
		if (m_lastAttackSwingEvent == attackSwingEvent)
		{
			return;
		}

		m_lastAttackSwingEvent = attackSwingEvent;

		// Nothing to do here in these cases
		if (m_lastAttackSwingEvent == attack_swing_event::Success ||
			m_lastAttackSwingEvent == attack_swing_event::Unknown)
		{
			return;
		}

		// Notify the client about the attack swing error event
		SendPacket([attackSwingEvent](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::AttackSwingError);
				packet << io::write<uint32>(attackSwingEvent);
				packet.Finish();
			});
	}

	void Player::OnXpLog(uint32 amount)
	{
		SendPacket([amount](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::XpLog);
				packet << io::write<uint32>(amount);
				packet.Finish();
			});
	}

	void Player::OnSpellDamageLog(uint64 targetGuid, uint32 amount, uint8 school, DamageFlags flags, const proto::SpellEntry& spell)
	{
		SendPacket([targetGuid, amount, school, flags, &spell](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::SpellDamageLog);
				packet
					<< io::write_packed_guid(targetGuid)
					<< io::write<uint32>(spell.id())
					<< io::write<uint32>(amount)
					<< io::write<uint8>(school)
					<< io::write<uint8>(flags);
				packet.Finish();
			});
	}

	void Player::OnNonSpellDamageLog(uint64 targetGuid, uint32 amount, DamageFlags flags)
	{
		SendPacket([targetGuid, amount, flags](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::NonSpellDamageLog);
				packet
					<< io::write_packed_guid(targetGuid)
					<< io::write<uint32>(amount)
					<< io::write<uint8>(flags);
				packet.Finish();
			});
	}

	void Player::OnSpeedChangeApplied(MovementType type, float speed, uint32 ackId)
	{
		SendPacket([type, speed, ackId](game::OutgoingPacket& packet)
			{
				static const uint16 moveOpCodes[MovementType::Count] = {
					game::realm_client_packet::ForceMoveSetWalkSpeed,
					game::realm_client_packet::ForceMoveSetRunSpeed,
					game::realm_client_packet::ForceMoveSetRunBackSpeed,
					game::realm_client_packet::ForceMoveSetSwimSpeed,
					game::realm_client_packet::ForceMoveSetSwimBackSpeed,
					game::realm_client_packet::ForceMoveSetTurnRate,
					game::realm_client_packet::ForceSetFlightSpeed,
					game::realm_client_packet::ForceSetFlightBackSpeed
				};

				packet.Start(moveOpCodes[type]);
				packet
					<< io::write<uint32>(ackId)
					<< io::write<float>(speed);
				packet.Finish();
			});
	}

	void Player::OnTeleport(const Vector3& position, const Radian& facing)
	{
		const uint32 ackId = m_character->GenerateAckId();

		// Generate pending movement change
		PendingMovementChange change;
		change.changeType = MovementChangeType::Teleport;
		change.timestamp = GetAsyncTimeMs();
		change.counter = ackId;
		change.teleportInfo.x = position.x;
		change.teleportInfo.y = position.y;
		change.teleportInfo.z = position.z;
		change.teleportInfo.o = facing.GetValueRadians();
		m_character->PushPendingMovementChange(change);

		// Send notification
		SendPacket([this, ackId](game::OutgoingPacket& packet) {
			packet.Start(game::realm_client_packet::MoveTeleportAck);
			packet
				<< io::write_packed_guid(this->m_character->GetGuid())
				<< io::write<uint32>(ackId)
				<< this->m_character->GetMovementInfo();
			packet.Finish();
		});
	}
}
