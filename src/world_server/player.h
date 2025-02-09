// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "realm_connector.h"
#include "vector_sink.h"
#include "game_server/character_data.h"
#include "game/chat_type.h"
#include "game/vendor.h"
#include "game_server/game_object_s.h"
#include "game_server/game_player_s.h"
#include "game_server/tile_index.h"
#include "game_server/tile_subscriber.h"
#include "game_protocol/game_protocol.h"

namespace mmo
{
	class LootInstance;

	namespace proto
	{
		class TrainerEntry;
		class VendorEntry;
		class Project;
	}

	/// @brief This class represents the connection to a player on a realm server that this
	///	       world node is connected to.
	class Player final : public TileSubscriber, public NetUnitWatcherS, public NetPlayerWatcher, public std::enable_shared_from_this<Player>
	{
	public:
		explicit Player(PlayerManager& manager, RealmConnector& realmConnector, std::shared_ptr<GamePlayerS> characterObject,
		                CharacterData characterData, const proto::Project& project);
		~Player() override;

	public:
		/// @brief Sends a network packet directly to the client.
		/// @tparam F Type of the packet generator function.
		/// @param generator The packet generator function.
		template<class F>
		void SendPacket(F generator, bool flush = true) const
		{
			std::vector<char> buffer;
			io::VectorSink sink(buffer);

			typename game::Protocol::OutgoingPacket packet(sink);
			generator(packet);

			// Send the proxy packet to the realm server
			m_connector.SendProxyPacket(m_character->GetGuid(), packet.GetId(), packet.GetSize(), buffer, flush);
		}

	public:
		/// @copydoc TileSubscriber::GetGameUnit
		GameUnitS& GetGameUnit() const override { return *m_character; }

		void NotifyObjectsUpdated(const std::vector<GameObjectS*>& objects) const override;

		/// @copydoc TileSubscriber::NotifyObjectsSpawned
		void NotifyObjectsSpawned(const std::vector<GameObjectS*>& object) const override;

		/// @copydoc TileSubscriber::NotifyObjectsDespawned
		void NotifyObjectsDespawned(const std::vector<GameObjectS*>& object) const override;

		/// @copydoc TileSubscriber::SendPacket
		void SendPacket(game::Protocol::OutgoingPacket& packet, const std::vector<char>& buffer, bool flush = true) override;

		void HandleProxyPacket(game::client_realm_packet::Type opCode, std::vector<uint8>& buffer);

		void LocalChatMessage(ChatType type, const std::string& message);

		void SaveCharacterData() const;

		void OpenLootDialog(std::shared_ptr<LootInstance> lootInstance, std::shared_ptr<GameObjectS> source);

		void CloseLootDialog();

		bool IsLooting() const;

		void OnItemCreated(std::shared_ptr<GameItemS> item, uint16 slot);

		void OnItemUpdated(std::shared_ptr<GameItemS> item, uint16 slot);

		void OnItemDestroyed(std::shared_ptr<GameItemS> item, uint16 slot);

	public:
		TileIndex2D GetTileIndex() const;

	private:
		/// @brief Executed when the character spawned on a world instance.
		/// @param instance The world instance that the character spawned in.
		void OnSpawned(WorldInstance& instance);

		void OnDespawned(GameObjectS& object);

		void OnTileChangePending(VisibilityTile& oldTile, VisibilityTile& newTile);

		void SpawnTileObjects(VisibilityTile& tile);

		void Kick();

		void SendTrainerBuyError(uint64 trainerGuid, trainer_result::Type result) const;

		void SendTrainerBuySucceeded(uint64 trainerGuid, uint32 spellId) const;

		void SendQuestDetails(uint64 questgiverGuid, const proto::QuestEntry& quest);

		void SendQuestReward(uint64 questgiverGuid, const proto::QuestEntry& quest);

	public:
		/// @brief Gets the character guid.
		[[nodiscard]] uint64 GetCharacterGuid() const noexcept { return m_character->GetGuid(); }

	private:
		// Client Network Handlers, Implemented in player.cpp

		/// Handles the client's request to change the selected game object.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnSetSelection(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles a client's movement op code. Also does movement validation.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnMovement(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to cast a known spell.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnSpellCast(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to start auto attacking a target.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnAttackSwing(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to stop auto attacking his current target.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnAttackStop(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to revive his dead controlled player character.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnReviveRequest(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles a client ack packet for a change in movement speed or major movement capability changes or teleports.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnClientAck(uint16 opCode, uint32 size, io::Reader& contentReader);

	private:
		// Client Network Handlers, Implemented in player_inventory_handlers.cpp

		/// Handles the client's request to store an item from the currently looted game object into his inventory at the best possible slot.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnAutoStoreLootItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to equip an item from his inventory in the best possible slot.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnAutoEquipItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnAutoStoreBagItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to swap two items in his inventory.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnSwapItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnSwapInvItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to split a stack of items in his inventory into multiple stacks.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnSplitItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnAutoEquipItemSlot(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to destroy an item from his inventory.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnDestroyItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to start looting a lootable game object.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnLoot(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to look money from the currently looted game object.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnLootMoney(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to stop looting the currently looted game object.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnLootRelease(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to use an item from his inventory or of an equipped item. This will trigger the item's
		///	assigned spells with trigger type set to "On Use" if any.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnUseItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to increase an attribute with attribute points.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnAttributePoint(uint16 opCode, uint32 size, io::Reader& contentReader);

	private:
		// Client Network Handlers, Implemented in player_npc_handlers.cpp

		/// Handles the client's request to interact with an interactable friendly npc with multiple options like dynamic menus, simply static text,
		///	questgivers, vendors, trainers etc.. This is only expected to be sent when an npc has nothing specifically to offer like being only a quest
		///	giver and nothing else. Otherwise, the client is expected to send a specific packet to handle the specific interaction type like the "OnTrainerMenu" method.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnGossipHello(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to sell an item from his inventory to the currently interacted vendor npc.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnSellItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to buy an item from the currently interacted vendor npc.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnBuyItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to buy a spell from the currently interacted trainer npc.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnTrainerBuySpell(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to get the current status of a quest giver npc to display an icon above the npcs heads like a yellow "!" to
		///	indicate that this quest giver has quests available for the player character to accept.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnQuestGiverStatusQuery(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// 
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnQuestGiverCompleteQuest(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to ask for an npc's trainer menu. This is expected to be sent when the npc is just a trainer and nothing more.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnTrainerMenu(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to ask for an npc's vendor inventory. This is expected to be sent when the npc is just a vendor and nothing more.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnListInventory(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to ask for an npc's quest dialog. This is expected to be sent when the npc is just a quest giver / quest acceptor and nothing more.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnQuestGiverHello(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to accept a quest from the current quest giver.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnAcceptQuest(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to abandon a quest from his quest log. This will also make the player loose progress of that quest.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnAbandonQuest(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to query a quest giver for an offered quest.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnQuestGiverQueryQuest(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to query a quest giver for an offered quest.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void QuestGiverChooseQuestReward(uint16 opCode, uint32 size, io::Reader& contentReader);

	private:
		// Implemented in player_dev_handlers.cpp

#if MMO_WITH_DEV_COMMANDS
		/// Handles the client's request to spawn a temporary monster at the players position. The monster will not respawn when defeated and not persist in the world on restart.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnCheatCreateMonster(uint16 opCode, uint32 size, io::Reader& contentReader) const;

		/// Handles the client's request to destroy a monster. This will remove the monster from the world. Temporary monsters will be removed entirely, static monsters will respawn
		///	if they are configured to do so in the game files.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnCheatDestroyMonster(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to teach a spell to the selected player or himself. This will add the spell to the players spellbook and make it available for casting.
		///	The spell will also be persisted in the database and will be available after a restart of the server.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnCheatLearnSpell(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to make a unit follow the player. This will make the unit follow the player until the creature dies or the player logs out, or the unit enters combat.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnCheatFollowMe(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to make a selected unit face the player.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnCheatFaceMe(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to increase the selected players level by a specific amount. This will increase the players level and adjust the players stats accordingly.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnCheatLevelUp(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to increase the selected players money by a specific amount.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnCheatGiveMoney(uint16 opCode, uint32 size, io::Reader& contentReader);

		/// Handles the client's request to add an item to the selected players inventory. This might create multiple stacks of this item depending on the amount to give.
		///	@param opCode The op code of the packet.
		///	@param size The size of the packet content in bytes, excluding the packet header.
		/// @param contentReader Reader object used to read the packets content bytes.
		void OnCheatAddItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnCheatWorldPort(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnCheatSpeed(uint16 opCode, uint32 size, io::Reader& contentReader);
#endif

	private:
		void OnSpellLearned(GameUnitS& unit, const proto::SpellEntry& spellEntry);

		void OnSpellUnlearned(GameUnitS& unit, const proto::SpellEntry& spellEntry);

		void HandleVendorGossip(const proto::VendorEntry& vendor, const GameCreatureS& vendorUnit);

		void SendVendorInventory(const proto::VendorEntry& vendor, const GameCreatureS& vendorUnit);

		void SendVendorInventoryError(uint64 vendorGuid, vendor_result::Type result);

		void HandleTrainerGossip(const proto::TrainerEntry& trainer, const GameCreatureS& trainerUnit);

		void SendTrainerList(const proto::TrainerEntry& trainer, const GameCreatureS& trainerUnit);

	public:
		void OnAttackSwingEvent(AttackSwingEvent attackSwingEvent) override;

		void OnXpLog(uint32 amount) override;

		void OnSpellDamageLog(uint64 targetGuid, uint32 amount, uint8 school, DamageFlags flags, const proto::SpellEntry& spell) override;

		void OnNonSpellDamageLog(uint64 targetGuid, uint32 amount, DamageFlags flags) override;

		void OnSpeedChangeApplied(MovementType type, float speed, uint32 ackId) override;

		void OnTeleport(uint32 mapId, const Vector3& position, const Radian& facing) override;

		void OnLevelUp(uint32 newLevel, int32 healthDiff, int32 manaDiff, int32 staminaDiff, int32 strengthDiff,
			int32 agilityDiff, int32 intDiff, int32 spiritDiff, int32 talentPoints, int32 attributePoints) override;

		void OnSpellModChanged(SpellModType type, uint8 effectIndex, SpellModOp op, int32 value) override;

		void OnQuestKillCredit(const proto::QuestEntry&, uint64 guid, uint32 entry, uint32 count, uint32 maxCount) override;

		void OnQuestDataChanged(uint32 questId, const QuestStatusData& data) override;

		void OnQuestCompleted(uint64 questgiverGuid, uint32 questId, uint32 rewardedXp, uint32 rewardMoney) override;

	private:
		PlayerManager& m_manager;
		RealmConnector& m_connector;
		std::shared_ptr<GamePlayerS> m_character;
		WorldInstance* m_worldInstance { nullptr };
		CharacterData m_characterData;
		scoped_connection_container m_characterConnections;
		const proto::Project& m_project;
		AttackSwingEvent m_lastAttackSwingEvent{ attack_swing_event::Unknown };
		std::shared_ptr<LootInstance> m_loot{ nullptr };
		std::shared_ptr<GameObjectS> m_lootSource{ nullptr };

		scoped_connection_container m_lootSignals;
		scoped_connection m_onLootSourceDespawned;
	};

}
