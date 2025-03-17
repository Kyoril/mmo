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
#include "game_server/universe.h"

namespace mmo
{
	Player::Player(PlayerManager& playerManager, RealmConnector& realmConnector, std::shared_ptr<GamePlayerS> characterObject, CharacterData characterData, const proto::Project& project, WorldInstance& instance)
		: m_manager(playerManager)
		, m_connector(realmConnector)
		, m_character(std::move(characterObject))
		, m_characterData(std::move(characterData))
		, m_project(project)
		, m_groupUpdate(instance.GetUniverse().GetTimers())
	{
		m_character->SetNetUnitWatcher(this);
		m_character->SetPlayerWatcher(this);

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

		// Group update signal
		m_groupUpdate.ended.connect([&]()
			{
				const Vector3 location(m_character->GetPosition());

				// Get group id
				auto groupId = m_character->GetGroupId();
				std::vector<uint64> nearbyMembers;

				// Determine nearby party members
				instance.GetUnitFinder().FindUnits(Circle(location.x, location.z, 40.0f), [this, groupId, &nearbyMembers](GameUnitS& unit) -> bool
					{
						// Only characters
						if (!unit.IsPlayer())
						{
							return true;
						}

						// Check characters group
						GamePlayerS* character = static_cast<GamePlayerS*>(&unit);
						if (character->GetGroupId() == groupId &&
							character->GetGuid() != GetCharacterGuid())
						{
							nearbyMembers.push_back(character->GetGuid());
						}

						return true;
					});

				// Send packet to world node
				m_connector.SendCharacterGroupUpdate(*m_character, nearbyMembers);
				m_groupUpdate.SetEnd(GetAsyncTimeMs() + constants::OneSecond * 3);
			});


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

	void Player::UpdateCharacterGroup(uint64 groupId)
	{
		if (m_character)
		{
			uint64 oldGroup = m_character->GetGroupId();
			m_character->SetGroupId(groupId);

			m_character->Invalidate(object_fields::Health);
			m_character->Invalidate(object_fields::MaxHealth);

			// For every group member in range, also update health fields
			TileIndex2D tileIndex;
			m_character->GetTileIndex(tileIndex);
			ForEachSubscriberInSight(
				m_character->GetWorldInstance()->GetGrid(),
				tileIndex,
				[oldGroup, groupId](TileSubscriber& subscriber)
				{
					if (auto* player = dynamic_cast<GamePlayerS*>(&subscriber.GetGameUnit()))
					{
						if ((oldGroup != 0 && player->GetGroupId() == oldGroup) ||
							(groupId != 0 && player->GetGroupId() == groupId))
						{
							player->Invalidate(object_fields::Health);
							player->Invalidate(object_fields::MaxHealth);
						}
					}
				});
		}

		if (groupId != 0)
		{
			// Trigger next group member update
			m_groupUpdate.SetEnd(GetAsyncTimeMs() + constants::OneSecond * 3);
		}
		else
		{
			// Stop group member update
			m_groupUpdate.Cancel();
		}
	}

	void Player::NotifyObjectsUpdated(const std::vector<GameObjectS*>& objects)
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

	void Player::NotifyObjectsSpawned(const std::vector<GameObjectS*>& objects)
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


			// Send movement packets
			if (object->IsUnit())
			{
				object->AsUnit().GetMover().SendMovementPackets(*this);
			}
		}

		// Flush packets
		m_connector.flush();
	}

	void Player::NotifyObjectsDespawned(const std::vector<GameObjectS*>& objects)
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
		case game::client_realm_packet::RandomRoll:
			OnRandomRoll(opCode, buffer.size(), reader);
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
		case game::client_realm_packet::CheatWorldPort:
			OnCheatWorldPort(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::CheatSpeed:
			OnCheatSpeed(opCode, buffer.size(), reader);
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

		case game::client_realm_packet::QuestGiverStatusQuery:
			OnQuestGiverStatusQuery(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::TrainerMenu:
			OnTrainerMenu(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::ListInventory:
			OnListInventory(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::QuestGiverHello:
			OnQuestGiverHello(opCode, buffer.size(), reader);
			break;

		case game::client_realm_packet::AcceptQuest:
			OnAcceptQuest(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::AbandonQuest:
			OnAbandonQuest(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::QuestGiverQueryQuest:
			OnQuestGiverQueryQuest(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::QuestGiverCompleteQuest:
			OnQuestGiverCompleteQuest(opCode, buffer.size(), reader);
			break;
		case game::client_realm_packet::QuestGiverChooseQuestReward:
			QuestGiverChooseQuestReward(opCode, buffer.size(), reader);
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
		case game::client_realm_packet::MoveEnded:
		case game::client_realm_packet::MoveSplineDone:
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
		DLOG("Player spawned on map " << instance.GetMapId() << ", instance " << instance.GetId() << "...");

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

		// Trigger group change to start synchronizing group data
		if (m_characterData.groupId != 0)
		{
			UpdateCharacterGroup(m_characterData.groupId);
		}
	}

	void Player::OnDespawned(GameObjectS& object)
	{
		// No longer watch for network events
		m_character->SetNetUnitWatcher(nullptr);

		SaveCharacterData();

		// Find our tile
		TileIndex2D tileIndex = GetTileIndex();
		VisibilityTile& tile = m_worldInstance->GetGrid().RequireTile(tileIndex);
		tile.GetWatchers().remove(this);
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

		m_connector.SendCharacterData(m_worldInstance->GetMapId(), m_worldInstance->GetId(), *m_character);
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
		m_connector.NotifyWorldInstanceLeft(m_character->GetGuid(), auth::world_left_reason::Disconnect);
		m_manager.RemovePlayer(shared_from_this());
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

	void Player::SendQuestDetails(uint64 questgiverGuid, const proto::QuestEntry& quest)
	{
		SendPacket([questgiverGuid, quest, this](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::QuestGiverQuestDetails);
				packet
					<< io::write<uint64>(questgiverGuid)
					<< io::write<uint32>(quest.id())
					<< io::write_dynamic_range<uint8>(quest.name())
					<< io::write_dynamic_range<uint16>(quest.detailstext())
					<< io::write_dynamic_range<uint16>(quest.objectivestext())
					<< io::write<uint32>(quest.suggestedplayers());

				if (quest.flags() & quest_flags::HiddenRewards)
				{
					packet
						<< io::write<uint32>(0) << io::write<uint32>(0) << io::write<uint32>(0);
				}
				else
				{
					packet
						<< io::write<uint32>(quest.rewarditemschoice_size());
					for (const auto& reward : quest.rewarditemschoice())
					{
						packet
							<< io::write<uint32>(reward.itemid())
							<< io::write<uint32>(reward.count());
						const auto* item = m_project.items.getById(reward.itemid());
						packet
							<< io::write<uint32>(item ? item->displayid() : 0);
					}

					packet
						<< io::write<uint32>(quest.rewarditems_size());
					for (const auto& reward : quest.rewarditems())
					{
						packet
							<< io::write<uint32>(reward.itemid())
							<< io::write<uint32>(reward.count());
						const auto* item = m_project.items.getById(reward.itemid());
						packet
							<< io::write<uint32>(item ? item->displayid() : 0);
					}

					packet
						<< io::write<uint32>(quest.rewardmoney());
				}

				packet << io::write<uint32>(quest.rewardspell());
				packet.Finish();
			});
	}

	void Player::SendQuestReward(uint64 questgiverGuid, const proto::QuestEntry& quest)
	{
		// Quest is completed, offer the rewards to the player
		SendPacket([questgiverGuid, quest, this](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::QuestGiverOfferReward);
				packet
					<< io::write<uint64>(questgiverGuid)
					<< io::write<uint32>(quest.id())
					<< io::write_dynamic_range<uint8>(quest.name())
					<< io::write_dynamic_range<uint16>(quest.offerrewardtext());

				packet
					<< io::write<uint32>(quest.rewarditemschoice_size());
				for (const auto& reward : quest.rewarditemschoice())
				{
					packet
						<< io::write<uint32>(reward.itemid())
						<< io::write<uint32>(reward.count());
					const auto* item = m_project.items.getById(reward.itemid());
					packet
						<< io::write<uint32>(item ? item->displayid() : 0);
				}

				packet
					<< io::write<uint32>(quest.rewarditems_size());
				for (const auto& reward : quest.rewarditems())
				{
					packet
						<< io::write<uint32>(reward.itemid())
						<< io::write<uint32>(reward.count());
					const auto* item = m_project.items.getById(reward.itemid());
					packet
						<< io::write<uint32>(item ? item->displayid() : 0);
				}

				packet
					<< io::write<uint32>(quest.rewardmoney());

				packet
					<< io::write<uint32>(quest.rewardxp())
					<< io::write<uint32>(quest.rewardspell());
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

		// Should the character currently be moved automatically?
		if (m_character->GetMover().IsMoving())
		{
			// Just ignore this movement op code for now
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
		if (!info.IsFalling() && m_character->GetMovementInfo().IsFalling() && (opCode != game::client_realm_packet::MoveFallLand && opCode != game::client_realm_packet::MoveEnded))
		{
			ELOG("Client tried to remove FALLING flag in non-jump packet!");
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
		else if (opCode == game::client_realm_packet::MoveFallLand)
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
		case game::client_realm_packet::MoveEnded: opCode = game::realm_client_packet::MoveEnded; break;
		case game::client_realm_packet::MoveSplineDone: opCode = game::realm_client_packet::MoveSplineDone; break;
		default:
			WLOG("Received unknown movement packet " << log_hex_digit(opCode) << ", ensure that it is handled");
		}

		if (opCode == game::realm_client_packet::MoveEnded)
		{
			// Height diff of 3.1 is to be expected as difference between nav mesh and real ground due to nav mesh simplification so we will tolerate it here
			const Vector3 target = m_character->GetMover().GetTarget();
			if (std::abs(target.x - info.position.x) > FLT_EPSILON || std::abs(target.z - info.position.z) > FLT_EPSILON || std::abs(target.y - info.position.y) > 3.1f)
			{
				ELOG("Player ended movement but target position was different from expected location: Received " << info.position << ", expected " << m_character->GetMover().GetTarget());
				//return;
			}
		}
		else if (!m_character->GetMovementInfo().IsChangingPosition() && opCode != game::realm_client_packet::MoveSplineDone)
		{
			if (info.position != m_character->GetPosition())
			{
				ELOG("[OPCode " << opCode << "] Pos changed on client while it should not be able to do so based on server info");
				ELOG("\tS: " << m_character->GetPosition());
				ELOG("\tC: " << info.position);
				//return;
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

		// For now, we simply reset the player health back to the maximum health value.
		// We will need to teleport the player back to it's binding point once teleportation is supported!
		m_character->Set<uint32>(object_fields::Health, m_character->Get<uint32>(object_fields::MaxHealth) / 2);
		if (m_character->Get<uint32>(object_fields::MaxMana) > 1)
		{
			m_character->Set<uint32>(object_fields::Mana, m_character->Get<uint32>(object_fields::MaxMana) / 2);
		}

		m_character->StartRegeneration();

		m_character->Teleport(m_character->GetBindMap(), m_character->GetBindPosition(), m_character->GetBindFacing());
	}

	void Player::OnRandomRoll(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		int32 min, max;
		if (!(contentReader >> io::read<int32>(min) >> io::read<int32>(max)))
		{
			ELOG("Failed to read RandomRoll packet!");
			return;
		}

		// Ensure its correctly ordered
		const int32 tmp = min;
		min = std::min(min, max);
		max = std::max(tmp, max);

		// Roll
		std::uniform_int_distribution distribution(min, max);
		const int32 roll = min == max ? min : distribution(randomGenerator);

		std::vector<char> buffer;
		io::VectorSink sink{ buffer };
		game::OutgoingPacket packet{ sink };
		packet.Start(game::realm_client_packet::RandomRollResult);
		packet
			<< io::write<uint64>(m_character->GetGuid())
			<< io::write<int32>(min)
			<< io::write<int32>(max)
			<< io::write<int32>(roll);
		packet.Finish();

		// Send result to all nearby units including the player
		TileIndex2D center;
		m_character->GetTileIndex(center);
		ForEachSubscriberInSight(m_character->GetWorldInstance()->GetGrid(),
			center, [&packet, &buffer](TileSubscriber& subscriber)
			{
				subscriber.SendPacket(packet, buffer, true);
			});
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

			if (!(info.movementFlags & movement_flags::Falling))
			{
				WLOG("Teleport ack received but player is not falling while it should be!");
			}

			if (info.movementFlags & (movement_flags::Moving | movement_flags::Turning | movement_flags::Strafing))
			{
				WLOG("Teleport ack received but player is moving");
			}

			DLOG("Received teleport ack towards " << change.teleportInfo.x << "," << change.teleportInfo.y << "," << change.teleportInfo.z);
			break;
		}

		// Apply movement info
		m_character->ApplyMovementInfo(info);
		//m_character->Relocate(info.position, info.facing);

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

	void Player::OnTeleport(uint32 mapId, const Vector3& position, const Radian& facing)
	{
		ASSERT(m_worldInstance);

		if (mapId == m_worldInstance->GetMapId())
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

				auto movementInfo = m_character->GetMovementInfo();
				movementInfo.movementFlags |= movement_flags::Falling;
				movementInfo.movementFlags &= ~(movement_flags::Moving | movement_flags::Turning | movement_flags::Strafing);

				packet.Start(game::realm_client_packet::MoveTeleportAck);
				packet
					<< io::write_packed_guid(this->m_character->GetGuid())
					<< io::write<uint32>(ackId)
					<< movementInfo;
				packet.Finish();
				});
		}
		else
		{
			// Send transfer pending state. This will show up the loading screen at the client side and will
			// tell the realm where our character should be sent to
			SendPacket([mapId](game::OutgoingPacket& packet) {
				packet.Start(game::realm_client_packet::TransferPending);
				packet << io::write<uint32>(mapId);
				packet.Finish();
				});

			// Initialize teleport request at realm
			const uint64 guid = m_character->GetGuid();
			m_connector.SendTeleportRequest(guid, mapId, position, facing);

			// No longer watch tile
			VisibilityTile& tile = m_worldInstance->GetGrid().RequireTile(GetTileIndex());
			tile.GetWatchers().remove(this);

			// Remove the character from the world (this will save the character)
			m_worldInstance->RemoveGameObject(*m_character);

			// Notify realm that our character left this world instance (this will commit the pending teleport request)
			m_connector.NotifyWorldInstanceLeft(guid, auth::world_left_reason::Teleport);

			// Destroy player instance
			m_character.reset();
			m_manager.RemovePlayer(shared_from_this());
		}
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

		// Save character due to levelup
		SaveCharacterData();
	}

	void Player::OnSpellModChanged(SpellModType type, uint8 effectIndex, SpellModOp op, int32 value)
	{
		// TODO
	}

	void Player::OnQuestKillCredit(const proto::QuestEntry& quest, uint64 guid, uint32 entry, uint32 count, uint32 maxCount)
	{
		SendPacket([&quest, guid, entry, count, maxCount](game::OutgoingPacket& packet) {
			packet.Start(game::realm_client_packet::QuestUpdateAddKill);
			packet
				<< io::write<uint32>(quest.id())
				<< io::write<uint32>(entry)
				<< io::write<uint32>(count)
				<< io::write<uint32>(maxCount)
				<< io::write<uint64>(guid);
			packet.Finish();
			});
	}

	void Player::OnQuestDataChanged(uint32 questId, const QuestStatusData& data)
	{
		// Send quest data packet to realm server so that it will be persisted in the database
		m_connector.SendQuestData(m_character->GetGuid(), questId, data);

		// Notify client about completed quest
		if (data.status == quest_status::Complete)
		{
			SendPacket([&questId](game::OutgoingPacket& packet) {
				packet.Start(game::realm_client_packet::QuestUpdateComplete);
				packet
					<< io::write<uint32>(questId);
				packet.Finish();
				});
		}
	}

	void Player::OnQuestCompleted(uint64 questgiverGuid, uint32 questId, uint32 rewardedXp, uint32 rewardMoney)
	{
		SendPacket([&questgiverGuid, questId, rewardedXp, rewardMoney](game::OutgoingPacket& packet) {
			packet.Start(game::realm_client_packet::QuestGiverQuestComplete);
			packet
				<< io::write<uint64>(questgiverGuid)
				<< io::write<uint32>(questId)
				<< io::write<uint32>(rewardedXp)
				<< io::write<uint32>(rewardMoney);
			packet.Finish();
			});

		// Save character due to quest data change
		SaveCharacterData();
	}
}
