// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
#include "game/vendor.h"

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
		// Handle object field updates if any
		{
			// Prepare update packet
			std::vector<char> buffer;
			io::VectorSink sink(buffer);

			typename game::Protocol::OutgoingPacket packet(sink);
			packet.Start(game::realm_client_packet::UpdateObject);
			const size_t countPosition = sink.Position();
			packet << io::write<uint16>(objects.size());

			uint16 objectUpdateCount = objects.size();
			for (const auto& object : objects)
			{
				if (!object->HasFieldChanges())
				{
					objectUpdateCount--;
					continue;
				}

				object->WriteObjectUpdateBlock(packet, false);
			}

			sink.Overwrite(countPosition, reinterpret_cast<const char*>(&objectUpdateCount), sizeof(uint16));
			packet.Finish();

			if (objectUpdateCount > 0)
			{
				// Send the proxy packet to the realm server
				m_connector.SendProxyPacket(m_character->GetGuid(), packet.GetId(), packet.GetSize(), buffer, false);
			}
		}
		
		// Handle aura updates
		for (auto& object : objects)
		{
			// Only units have auras
			if (!object->IsUnit())
			{
				continue;
			}

			GameUnitS* unit = dynamic_cast<GameUnitS*>(object);
			ASSERT(unit);

			// TODO: Check if an aura update is needed and only update auras if necessary

			SendPacket([unit](game::OutgoingPacket& outPacket)
				{
					outPacket.Start(game::realm_client_packet::AuraUpdate);
					unit->BuildAuraPacket(outPacket);
					outPacket.Finish();
				}, false);
		}

		m_connector.flush();
	}

	void Player::NotifyObjectsSpawned(const std::vector<GameObjectS*>& objects) const
	{
		// Send spawn packet
		SendPacket([&objects](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::realm_client_packet::UpdateObject);
			outPacket << io::write<uint16>(objects.size());
			for (const auto& object : objects)
			{
				object->WriteObjectUpdateBlock(outPacket);
			}
			outPacket.Finish();
		}, true);

		// Send aura update packets for spawned units
		for (const auto& object : objects)
		{
			if (!object->IsUnit())
			{
				continue;
			}

			const GameUnitS* unit = dynamic_cast<GameUnitS*>(object);
			if (!unit)
			{
				continue;
			}

			// Send initial aura update for spawned unit
			SendPacket([unit](game::OutgoingPacket& outPacket)
			{
				outPacket.Start(game::realm_client_packet::AuraUpdate);
				unit->BuildAuraPacket(outPacket);
				outPacket.Finish();
			}, false);
		}

		// Flush packets
		m_connector.flush();
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

	void Player::SendPacket(game::Protocol::OutgoingPacket& packet, const std::vector<char>& buffer, bool flush)
	{
		m_connector.SendProxyPacket(m_character->GetGuid(), packet.GetId(), packet.GetSize(), buffer, flush);
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

#if MMO_WITH_DEV_COMMANDS
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
		case game::client_realm_packet::CheatLevelUp:
			OnCheatLevelUp(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::CheatGiveMoney:
			OnCheatGiveMoney(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::CheatAddItem:
			OnCheatAddItem(opCode, buffer.size(), reader);
			break;
#endif

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
		case game::client_realm_packet::GossipHello:
			OnGossipHello(opCode, buffer.size(), reader);
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

		case game::client_realm_packet::SellItem:
			OnSellItem(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::BuyItem:
			OnBuyItem(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::UseItem:
			OnUseItem(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::AttributePoint:
			OnAttributePoint(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::TrainerBuySpell:
			OnTrainerBuySpell(opCode, buffer.size(), reader);
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

		// Cast passive spells after spawn, so that SpellMods are sent AFTER the spawn packet
		SpellTargetMap target;
		target.SetTargetMap(spell_cast_target_flags::Self);
		for (const auto& spell : m_character->GetSpells())
		{
			if (spell->attributes(0) & spell_attributes::Passive)
			{
				m_character->CastSpell(target, *spell, 0, true, 0);
			}
		}

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

	void Player::SendTrainerBuyError(uint64 trainerGuid, trainer_result::Type result) const
	{
		SendPacket([trainerGuid, result](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::TrainerBuyError);
				packet << io::write<uint64>(trainerGuid) << io::write<uint8>(result);
				packet.Finish();
			});
	}

	void Player::SendTrainerBuySucceeded(uint64 trainerGuid, uint32 spellId) const
	{
		SendPacket([trainerGuid, spellId](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::TrainerBuySucceeded);
				packet << io::write<uint64>(trainerGuid) << io::write<uint32>(spellId);
				packet.Finish();
			});
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

		if (spell->attributes(0) & spell_attributes::Passive)
		{
			ELOG("Tried to cast passive spell " << spellId);
			return;
		}

		// Get the cast time of this spell
		int64 castTime = spell->casttime();
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

	void Player::OnGossipHello(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 objectGuid;
		if (!(contentReader >> io::read<uint64>(objectGuid)))
		{
			WLOG("Failed to read object guid");
			return;
		}

		const GameCreatureS* unit = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(objectGuid);
		if (!unit)
		{
			return;
		}

		DLOG("Requested gossip menu from npc " << log_hex_digit(objectGuid) << " (" << unit->GetEntry().name() << ")");

		// Is this unit a trainer?
		const proto::UnitEntry& unitEntry = unit->GetEntry();
		const proto::TrainerEntry* trainer = nullptr;
		const proto::VendorEntry* vendor = nullptr;
		if (unitEntry.trainerentry() != 0)
		{
			trainer = m_project.trainers.getById(unitEntry.trainerentry());
		}

		if (unitEntry.vendorentry() != 0)
		{
			vendor = m_project.vendors.getById(unitEntry.vendorentry());
		}

		if (vendor && !trainer)
		{
			HandleVendorGossip(*vendor, *unit);
		}
		else if (trainer && !vendor)
		{
			HandleTrainerGossip(*trainer, *unit);
		}
		else
		{
			// TODO: Handle other gossip
		}
	}

	void Player::OnSellItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 vendorGuid, itemGuid;
		if (!(contentReader >> io::read<uint64>(vendorGuid) >> io::read<uint64>(itemGuid)))
		{
			WLOG("Failed to read vendor guid and item guid");
			return;
		}

		// Find vendor
		GameCreatureS* vendor = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(vendorGuid);
		if (!vendor)
		{
			ELOG("Can't find vendor!");
			return;
		}

		// Check basic interaction
		if (!vendor->IsInteractable(*m_character))
		{
			return;
		}

		uint16 itemSlot = 0;
		if (!m_character->GetInventory().FindItemByGUID(itemGuid, itemSlot))
		{
			ELOG("Can't find item!");
			return;
		}

		// Find the item by it's guid
		auto item = m_character->GetInventory().GetItemAtSlot(itemSlot);
		if (!item)
		{
			ELOG("Can't find item at slot!");
			return;
		}

		uint32 stack = item->GetStackCount();
		uint32 money = stack * item->GetEntry().sellprice();
		if (money == 0)
		{
			ELOG("Can't sell item!");
			return;
		}

		// TODO: Overflow protection!
		m_character->GetInventory().RemoveItem(itemSlot, stack, true);
		m_character->Set<uint32>(object_fields::Money, m_character->Get<uint32>(object_fields::Money) + money);
	}

	void Player::OnBuyItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 vendorGuid = 0;
		uint32 item = 0;
		uint8 count = 0;
		if (!(contentReader >> io::read<uint64>(vendorGuid) >> io::read<uint32>(item) >> io::read<uint8>(count)))
		{
			WLOG("Failed to read BuyItem packet!");
			return;
		}

		if (count == 0) count = 1;

		if (!m_character->IsAlive())
		{
			ELOG("Can't buy items while character is dead!");
			return;
		}

		// Find item entry
		const proto::ItemEntry* itemEntry = m_project.items.getById(item);
		if (itemEntry == nullptr)
		{
			ELOG("Player wants to buy unknown item!");
			return;
		}

		uint32 buyCount = itemEntry->buycount();
		if (buyCount == 0) buyCount = 1;

		uint8 totalCount = count * buyCount;

		ASSERT(m_character->GetWorldInstance());
		const GameCreatureS* vendor = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(vendorGuid);
		if (!vendor)
		{
			ELOG("Unable to find vendor npc!");
			return;
		}

		// Check basic interaction
		if (!vendor->IsInteractable(*m_character))
		{
			return;
		}

		const auto* vendorEntry = m_project.vendors.getById(vendor->GetEntry().vendorentry());
		if (vendorEntry == nullptr || vendorEntry->items_size() <= 0)
		{
			ELOG("Npc has no vendor entry assigned and thus does not sell anything!");
			return;
		}

		if (auto vendorItemEntryIt = std::find_if(vendorEntry->items().begin(), vendorEntry->items().end(), [item](const proto::VendorItemEntry& vendorItem)
		{
			return vendorItem.item() == item;
		}); vendorItemEntryIt == vendorEntry->items().end())
		{
			ELOG("Vendor does not sell item!");
			return;
		}

		// Take money
		uint32 price = itemEntry->buyprice() * count;
		uint32 money = m_character->Get<uint32>(object_fields::Money);
		if (money < price)
		{
			ELOG("Not enough money to buy item from vendor");
			return;
		}

		std::map<uint16, uint16> addedBySlot;
		if (auto result = m_character->GetInventory().CreateItems(*itemEntry, totalCount, &addedBySlot); result != inventory_change_failure::Okay)
		{
			ELOG("Failed to create items in inventory!");
			//m_character->inventoryChangeFailure(result, nullptr, nullptr);
			return;
		}

		m_character->Set<uint32>(object_fields::Money, money - price);

		// Send push notifications
		for (auto& slot : addedBySlot)
		{
			auto item = m_character->GetInventory().GetItemAtSlot(slot.first);
			if (item)
			{
				// TODO: Send notification
			}
		}
	}

	void Player::OnAttributePoint(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 attributeId;
		if (!(contentReader >> io::read<uint32>(attributeId)))
		{
			ELOG("Failed to read AttributePoint packet!");
			return;
		}

		if (attributeId >= 5)
		{
			ELOG("Invalid attribute id referenced, failed to add attribute point");
			return;
		}

		// TODO: Add attribute point to character
		m_character->AddAttributePoint(attributeId);
	}

	void Player::OnUseItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 bagId = 0, slotId = 0;
		uint64 itemGuid = 0;
		SpellTargetMap targetMap;
		if (!(contentReader
			>> io::read<uint8>(bagId)
			>> io::read<uint8>(slotId)
			>> io::read<uint64>(itemGuid)
			>> targetMap))
		{
			ELOG("Could not read packet");
			return;
		}

		// Get item
		auto item = m_character->GetInventory().GetItemAtSlot(Inventory::GetAbsoluteSlot(bagId, slotId));
		if (!item)
		{
			WLOG("Item not found! Bag: " << uint16(bagId) << "; Slot: " << uint16(slotId));
			return;
		}

		if (item->GetGuid() != itemGuid)
		{
			WLOG("Item GUID does not match. We look for " << log_hex_digit(itemGuid) << " but found " << log_hex_digit(item->GetGuid()));
			return;
		}

		auto& entry = item->GetEntry();

		// Find all OnUse spells
		for (int i = 0; i < entry.spells_size(); ++i)
		{
			const auto& spell = entry.spells(i);
			if (!spell.spell())
			{
				WLOG("No spell entry");
				continue;
			}

			// Spell effect has to be triggered "on use"
			if (spell.trigger() != item_spell_trigger::OnUse)
			{
				continue;
			}

			// Look for the spell entry
			const auto* spellEntry = m_project.spells.getById(spell.spell());
			if (!spellEntry)
			{
				WLOG("Could not find spell by id " << spell.spell());
				continue;
			}

			// Cast the spell
			uint64 time = spellEntry->casttime();
			SpellCastResult result = m_character->CastSpell(targetMap, *spellEntry, time, false, itemGuid);
			if (result != spell_cast_result::CastOkay)
			{
				SendPacket([itemGuid, &result, &spell](game::OutgoingPacket& packet)
					{
						packet.Start(game::realm_client_packet::SpellFailure);
						packet
							<< io::write_packed_guid(itemGuid)
							<< io::write<uint32>(spell.spell())
							<< io::write<GameTime>(GetAsyncTimeMs())
							<< io::write<uint8>(result);
						packet.Finish();
					});
			}
		}
	}

	void Player::OnTrainerBuySpell(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 trainerGuid;
		uint32 spellId;
		if (!(contentReader >> io::read<uint64>(trainerGuid) >> io::read<uint32>(spellId)))
		{
			ELOG("Failed to read TrainerBuySpell packet!");
			return;
		}

		DLOG("Player wants to buy trainer spell " << spellId << " from trainer " << log_hex_digit(trainerGuid));

		ASSERT(m_character);
		ASSERT(m_character->GetWorldInstance());

		if (m_character->HasSpell(spellId))
		{
			ELOG("Player already knows that spell!");
			return;
		}

		GameCreatureS* trainerNpc = m_character->GetWorldInstance()->FindByGuid<GameCreatureS>(trainerGuid);
		if (!trainerNpc)
		{
			ELOG("Unable to find trainer npc!");
			return;
		}

		const auto* trainerEntry = m_project.trainers.getById(trainerNpc->GetEntry().trainerentry());
		if (!trainerEntry)
		{
			ELOG("Trainer npc does not seem to be an actual trainer!");
			return;
		}

		// Check basic interaction
		if (!trainerNpc->IsInteractable(*m_character))
		{
			return;
		}

		for (const auto& trainerSpellEntry : trainerEntry->spells())
		{
			if (trainerSpellEntry.spell() == spellId)
			{
				const proto::SpellEntry* spellEntry = m_project.spells.getById(spellId);
				if (!spellEntry)
				{
					ELOG("Unknown spell " << spellId << " offered by trainer id " << log_hex_digit(trainerEntry->id()) << ", can't buy spell!");
					return;
				}

				if (m_character->GetLevel() < trainerSpellEntry.reqlevel())
				{
					SendTrainerBuyError(trainerGuid, trainer_result::FailedLevelTooLow);
					return;
				}

				if (!m_character->ConsumeMoney(trainerSpellEntry.spellcost()))
				{
					SendTrainerBuyError(trainerGuid, trainer_result::FailedNotEnoughMoney);
					return;
				}

				m_character->AddSpell(spellId);
				SendTrainerBuySucceeded(trainerGuid, spellId);
				return;
			}
		}
	}

#if MMO_WITH_DEV_COMMANDS
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
#endif

#if MMO_WITH_DEV_COMMANDS
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
#endif

#if MMO_WITH_DEV_COMMANDS
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
#endif

#if MMO_WITH_DEV_COMMANDS
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
#endif

#if MMO_WITH_DEV_COMMANDS
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
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatLevelUp(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint8 level;
		if (!(contentReader >> io::read<uint8>(level)))
		{
			ELOG("Missing level parameter!");
			return;
		}

		// Check if we have a player character in target
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		// Find target unit
		GamePlayerS* targetUnit = m_worldInstance->FindByGuid<GamePlayerS>(targetGuid);
		if (!targetUnit || targetUnit->GetTypeId() != ObjectTypeId::Player)
		{
			targetUnit = m_character.get();
		}

		if (!targetUnit)
		{
			ELOG("Unable to find target character!");
			return;
		}

		uint32 characterLevel = targetUnit->GetLevel();
		uint32 targetLevel = characterLevel + level;

		// Check for overflow
		if (characterLevel + level >= targetUnit->Get<uint32>(object_fields::MaxLevel))
		{
			targetLevel = targetUnit->Get<uint32>(object_fields::MaxLevel);
		}

		if (targetLevel == targetUnit->GetLevel())
		{
			ELOG("Character level is unchanged");
			return;
		}

		ASSERT(targetUnit->GetClassEntry());
		DLOG("Setting level of target to " << targetLevel);

		// Grant experience while level is not reached
		while(characterLevel < targetLevel)
		{
			targetUnit->RewardExperience(targetUnit->Get<uint32>(object_fields::NextLevelXp) - targetUnit->Get<uint32>(object_fields::Xp));
			ASSERT(targetUnit->GetLevel() > characterLevel);

			characterLevel = targetUnit->GetLevel();
		}

	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatGiveMoney(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 amount;
		if (!(contentReader >> io::read<uint32>(amount)))
		{
			ELOG("Missing amount parameter!");
			return;
		}

		// Check if we have a player character in target
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		// Find target unit
		GamePlayerS* targetPlayer = m_worldInstance->FindByGuid<GamePlayerS>(targetGuid);
		if (!targetPlayer)
		{
			targetPlayer = m_character.get();
		}

		if (!targetPlayer)
		{
			ELOG("Unable to find target character!");
			return;
		}

		uint32 money = targetPlayer->Get<uint32>(object_fields::Money);

		// Check for overflow
		if (money + amount <= money)
		{
			money = std::numeric_limits<uint32>::max();
		}
		else
		{
			money += amount;
		}

		DLOG("Setting money of target to " << money);
		targetPlayer->Set<uint32>(object_fields::Money, money);
	}
#endif

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatAddItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 itemId;
		uint8 count;
		if (!(contentReader >> io::read<uint32>(itemId) >> io::read<uint8>(count)))
		{
			ELOG("Failed to read CheatAddItem packet!");
			return;
		}

		// Check if we have a player character in target
		uint64 targetGuid = m_character->Get<uint64>(object_fields::TargetUnit);
		if (targetGuid == 0)
		{
			targetGuid = m_character->GetGuid();
		}

		// Find target unit
		GamePlayerS* targetPlayer = m_worldInstance->FindByGuid<GamePlayerS>(targetGuid);
		if (!targetPlayer)
		{
			targetPlayer = m_character.get();
		}

		if (!targetPlayer)
		{
			ELOG("Unable to find target character!");
			return;
		}

		if (count == 0)
		{
			count = 1;
		}

		const auto* itemEntry = m_project.items.getById(itemId);
		if (!itemEntry)
		{
			ELOG("Item with item id " << itemId << " does not exist!");
			return;
		}

		targetPlayer->GetInventory().CreateItems(*itemEntry, count);
	}
#endif

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

	void Player::HandleVendorGossip(const proto::VendorEntry& vendor, const GameCreatureS& vendorUnit)
	{
		constexpr float interactionDistance = 5.0f;

		if (!vendorUnit.IsAlive())
		{
			SendVendorInventoryError(vendorUnit.GetGuid(), vendor_result::VendorIsDead);
		}
		else if (vendorUnit.UnitIsEnemy(*m_character))
		{
			SendVendorInventoryError(vendorUnit.GetGuid(), vendor_result::VendorHostile);
		}
		else if (m_character->GetSquaredDistanceTo(vendorUnit.GetPosition(), true) > interactionDistance * interactionDistance)
		{
			SendVendorInventoryError(vendorUnit.GetGuid(), vendor_result::VendorTooFarAway);
		}
		else if (!m_character->IsAlive())
		{
			SendVendorInventoryError(vendorUnit.GetGuid(), vendor_result::CantShopWhileDead);
		}
		else if (vendor.items_size() == 0)
		{
			SendVendorInventoryError(vendorUnit.GetGuid(), vendor_result::VendorHasNoItems);
		}
		else
		{
			SendVendorInventory(vendor, vendorUnit);
		}
	}

	void Player::SendVendorInventory(const proto::VendorEntry& vendor, const GameCreatureS& vendorUnit)
	{
		SendPacket([&vendor, &vendorUnit, this](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::ListInventory);
			packet
				<< io::write<uint64>(vendorUnit.GetGuid())
				<< io::write<uint8>(vendor.items_size());

				uint32 index = 0;
				for (const auto& listEntry : vendor.items())
				{
					const proto::ItemEntry* item = m_project.items.getById(listEntry.item());
					if (!item)
					{
						continue;
					}

					packet
						<< io::write<uint32>(index++)
						<< io::write<uint32>(listEntry.item())
						<< io::write<uint32>(item->displayid())
						<< io::write<uint32>(listEntry.maxcount())
						<< io::write<uint32>(item->buyprice())
						<< io::write<uint32>(item->durability())
						<< io::write<uint32>(item->buycount())
						<< io::write<uint32>(listEntry.extendedcost())
						;
				}

			packet.Finish();
		});
	}

	void Player::SendVendorInventoryError(uint64 vendorGuid, vendor_result::Type result)
	{
		SendPacket([&vendorGuid, &result](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::ListInventory);
				packet
					<< io::write<uint64>(vendorGuid)
					<< io::write<uint8>(0)		// Inventory size is 0 which opens the door for error event handling
					<< io::write<uint8>(result);
				packet.Finish();
			});
	}

	void Player::HandleTrainerGossip(const proto::TrainerEntry& trainer, const GameCreatureS& trainerUnit)
	{
		constexpr float interactionDistance = 5.0f;

		if (!trainerUnit.IsAlive())
		{
			return;
		}
		if (trainerUnit.UnitIsEnemy(*m_character))
		{
			return;
		}
		if (m_character->GetSquaredDistanceTo(trainerUnit.GetPosition(), true) > interactionDistance * interactionDistance)
		{
			return;
		}
		if (!m_character->IsAlive())
		{
			return;
		}
		if (trainer.spells_size() == 0)
		{
			return;
		}
		
		SendTrainerList(trainer, trainerUnit);
	}

	void Player::SendTrainerList(const proto::TrainerEntry& trainer, const GameCreatureS& trainerUnit)
	{
		SendPacket([&trainer, &trainerUnit, this](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::TrainerList);
				packet
					<< io::write<uint64>(trainerUnit.GetGuid())
					<< io::write<uint16>(trainer.spells_size());

				uint32 index = 0;
				for (const auto& trainerSpellEntry : trainer.spells())
				{
					packet
						<< io::write<uint32>(trainerSpellEntry.spell())
						<< io::write<uint32>(trainerSpellEntry.spellcost())
						<< io::write<uint32>(trainerSpellEntry.reqlevel())
						<< io::write<uint32>(trainerSpellEntry.reqskill())
						<< io::write<uint32>(trainerSpellEntry.reqskillval())
						;
				}

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

	void Player::OnLevelUp(uint32 newLevel, int32 healthDiff, int32 manaDiff, int32 staminaDiff, int32 strengthDiff, int32 agilityDiff, int32 intDiff, int32 spiritDiff, int32 talentPoints, int32 attributePoints)
	{
		SendPacket([newLevel, healthDiff, manaDiff, staminaDiff, strengthDiff, agilityDiff, intDiff, spiritDiff, talentPoints, attributePoints](game::OutgoingPacket& packet) {
			packet.Start(game::realm_client_packet::LevelUp);
			packet
				<< io::write<uint8>(newLevel)
				<< io::write<int32>(healthDiff)
				<< io::write<int32>(manaDiff)
				<< io::write<int32>(staminaDiff)
				<< io::write<int32>(strengthDiff)
				<< io::write<int32>(agilityDiff)
				<< io::write<int32>(intDiff)
				<< io::write<int32>(spiritDiff)
				<< io::write<int32>(talentPoints)
				<< io::write<int32>(attributePoints);
			packet.Finish();
			});
	}

	void Player::OnSpellModChanged(SpellModType type, uint8 effectIndex, SpellModOp op, int32 value)
	{
		// TODO
	}
}
