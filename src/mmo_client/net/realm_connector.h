// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "realm_data.h"

#include "game_protocol/game_connector.h"
#include "base/big_number.h"
#include "base/signal.h"
#include "game/character_view.h"
#include "game/movement_type.h"

#include "asio/io_service.hpp"
#include "game/action_button.h"
#include "math/vector3.h"


namespace mmo
{
	class SpellTargetMap;
	class MovementInfo;

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
		/// Signal that is fired when the connection was lost.
		signal<void()> Disconnected;
		/// Signal that is fired when the client failed to enter a world with a specific error reason.
		signal<void(game::player_login_response::Type)> EnterWorldFailed;

		signal<void(uint32, Vector3, float)> VerifyNewWorld;

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
		uint32 m_realmId;

	public:
		/// Initializes a new instance of the RealmConnector class.
		/// @param io The io service to be used in order to create the internal socket.
		explicit RealmConnector(asio::io_service &io);

	private:
		/// Handles the LogonChallenge packet which is sent immediately after connecting to the realm server.
		///	@param packet The packet to parse.
		PacketParseResult OnAuthChallenge(game::IncomingPacket& packet);

		/// Handles the AuthSessionResponse packet which is sent after we sent the AuthChallenge response to the server.
		///	@param packet The packet to parse.
		PacketParseResult OnAuthSessionResponse(game::IncomingPacket& packet);

		/// Handles the CharEnum packet.
		///	@param packet The packet to parse.
		PacketParseResult OnCharEnum(game::IncomingPacket& packet);

		/// Handles the LoginVerifyWorld packet from the server which is sent when the character successfully entered a world node behind the scenes.
		///	@param packet The packet to parse.
		PacketParseResult OnLoginVerifyWorld(game::IncomingPacket& packet);

		/// Handles the EnterWorldFailed packet from the realm server.
		///	@param packet The packet to parse.
		PacketParseResult OnEnterWorldFailed(game::IncomingPacket& packet);

	public:
		// ~ Begin IConnectorListener
		bool connectionEstablished(bool success) override;
		void connectionLost() override;
		void connectionMalformedPacket() override;
		PacketParseResult connectionPacketReceived(game::IncomingPacket &packet) override;
		// ~ End IConnectorListener

		/// Represents a smart-pointer like struct which manages a packet handler registration and automatically unregisters the packet handler when it goes out of scope.
		///	This type can also be managed by a PacketHandlerHandleContainer to manage multiple packet handler registrations at once, which can be useful if you need to
		///	clear multiple specific packet handlers at once without having to free them manually.
		struct PacketHandlerRegistrationHandle final : NonCopyable
		{
		private:
			std::weak_ptr<RealmConnector> m_connector;
			uint16 m_opCode;

		public:
			/// Initializes a new instance of the PacketHandlerRegistrationHandle class and initializes it.
			///	@param connector The realm connector to register the packet handler at.
			///	@param opCode The opcode of the packet to register the handler for.
			PacketHandlerRegistrationHandle(RealmConnector& connector, const uint16 opCode)
				: m_connector(std::static_pointer_cast<RealmConnector>(connector.shared_from_this())), m_opCode(opCode)
			{
			}

			/// Move constructor.
			PacketHandlerRegistrationHandle(PacketHandlerRegistrationHandle&& other) noexcept
				: m_connector(std::move(other.m_connector)), m_opCode(other.m_opCode)
			{
				other.m_opCode = std::numeric_limits<uint16>::max();
			}

			/// Move assignment operator.
			PacketHandlerRegistrationHandle& operator=(PacketHandlerRegistrationHandle&& other) noexcept
			{
				m_connector = std::move(other.m_connector);
				m_opCode = other.m_opCode;
				other.m_opCode = std::numeric_limits<uint16>::max();
				return *this;
			}

			/// Destructor will automatically unregister the packet handler
			~PacketHandlerRegistrationHandle() override
			{
				const std::shared_ptr<RealmConnector> strongConnector = m_connector.lock();
				if (m_opCode != std::numeric_limits<uint16>::max() && strongConnector)
				{
					strongConnector->ClearPacketHandler(m_opCode);
				}
			}
		};

		/// This struct is a useful helper to store a number of PacketHandlerRegistrationHandle instances and allow adding new ones as well as clearing all of them
		///	which will automatically unregister all packet handler connections at once. This can be thought of as a smart pointer array.
		struct PacketHandlerHandleContainer final
		{
		private:
			std::vector<PacketHandlerRegistrationHandle> m_handles;

		public:
			/// Adds a new packet handler registration handle to the container.
			void Add(PacketHandlerRegistrationHandle&& handle)
			{
				m_handles.push_back(std::move(handle));
			}

			/// Clears all packet handler registration handles from the container and unregisters all associated packet handler registrations.
			void Clear()
			{
				m_handles.clear();
			}

			/// Determines whether the container is empty.
			[[nodiscard]] bool IsEmpty() const
			{
				return m_handles.empty();
			}

		public:
			/// Syntactic sugar to add a new packet handler registration handle to the container.
			PacketHandlerHandleContainer& operator+=(PacketHandlerRegistrationHandle&& handle)
			{
				m_handles.push_back(std::move(handle));
				return *this;
			}
		};

		/// Syntactic sugar implementation of RegisterPacketHandler to avoid having to use std::bind.
		template <class Instance, class Class, class... Args1>
		[[nodiscard]] PacketHandlerRegistrationHandle RegisterAutoPacketHandler(uint16 opCode, Instance& object, PacketParseResult(Class::* method)(Args1...))
		{
			RegisterPacketHandler(opCode, [&object, method](Args1... args) {
				return (object.*method)(Args1(args)...);
				});

			return { *this, opCode };
		}

	public:
		/// Sets the login data required to authenticate at the realm server. This data should have been retrieved from a login server before.
		///	@param accountName The name of the account to log in with in uppercase letters.
		///	@param sessionKey The session key received from the login server.
		void SetLoginData(const std::string& accountName, const BigNumber& sessionKey);

		/// Connects to the realm server using the given realm data received from the login server before.
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

		/// Sends the command to create a new character to the realm.
		void CreateCharacter(const std::string& name, uint8 race, uint8 characterClass, uint8 characterGender);

		/// Sends the command to delete a character to the realm.
		void DeleteCharacter(const CharacterView& character);

		/// Gets the name of the currently connected realm.
		const std::string& GetRealmName() const { return m_realmName; }

		/// Sends a movement packet for a character to the server. The character must be currently controlled by the client (the server will check this as well as perform other checks
		///	of the movement info like state changes etc.).
		///	@param characterId The id of the character to move.
		///	@param opCode The opcode of the movement packet to send indicating the type of movement performed.
		///	@param info The movement info to send.
		void SendMovementUpdate(uint64 characterId, uint16 opCode, const MovementInfo& info);

		/// Sends a packet to the server to set the selected target object of the currently controlled player.
		///	@param guid The guid of the object to select. Can be set to 0 to deselect the current target.
		void SetSelection(uint64 guid);

		/// FROM CONSOLE COMMAND: GAME MASTER only. Spawns a temporary monster which will not respawn if it is despawned somehow or if the server is restarted.
		///	@param entry The entry of the monster to spawn.
		void CreateMonster(uint32 entry);

		/// FROM CONSOLE COMMAND: GAME MASTER only. Destroys the selected monster. If this is a statically spawned monster, it will respawn after a while. If it was a temporary one,
		///	it will be despawned permanently.
		///	@param guid The guid of the monster to destroy.
		void DestroyMonster(uint64 guid);

		/// FROM CONSOLE COMMAND: GAME MASTER only. Makes the selected unit face the controlled player character.
		///	@param guid The guid of the unit which should face the player.
		void FaceMe(uint64 guid);

		/// FROM CONSOLE COMMAND: GAME MASTER only. Makes the selected unit follow the controlled player character.
		/// @param guid The guid of the unit which should follow the player.
		void FollowMe(uint64 guid);

		/// FROM CONSOLE COMMAND: GAME MASTER only. Learns a specific spell for the selected player (or the controlled player if no player is selected).
		///	@param spellId The id of the spell to learn.
		void LearnSpell(uint32 spellId);

		/// FROM CONSOLE COMMAND: GAME MASTER only. Increases the level of the selected player (or the controlled player if no player is selected) by a specific amount.
		///	@param level The amount of levels to increase the player by.
		void LevelUp(uint8 level);

		/// FROM CONSOLE COMMAND: GAME MASTER only. Gives the selected player (or the controlled player if no player is selected) a specific amount of money.
		///	@param amount The amount of money to give to the player. 10 = 10 copper, 100 = 1 silver, 10000 = 1 gold, etc.
		void GiveMoney(uint32 amount);

		void AddItem(uint32 itemId, uint8 count);

		void WorldPort(uint32 mapId, const Vector3& position, const Radian& facing);

		/// Sends a packet to the server to cast a specific spell. The controlled character must know the spell. This method can not be used to
		///	cast spells from items. Instead, use the UseItem method for this instead.
		///	@param spellId The id of the spell to cast.
		///	@param targetMap A map of targets for the spell. Must be properly filled to fulfill the spell's target requirements.
		void CastSpell(uint32 spellId, const SpellTargetMap& targetMap);

		/// Sends a packet to the server to request revival of the controlled character. On success, the player will probably be teleported by the server.
		void SendReviveRequest();

		/// Sends a movement speed change acknowledgement packet to the server. This has to be done for movement speed changes to take effect and for the
		///	server to not kick us for speed hacking. All parameters must match the servers notification from before, otherwise we will be kicked as well.
		///	Each acknowledgement has a unique id, as well as a server-side timeout.
		///	@param type The type of movement for which the speed change is acknowledged.
		///	@param ackId The id of the movement speed change acknowledgement.
		///	@param speed The new movement speed as received from the server before.
		///	@param movementInfo The movement info that was received from the server before.
		void SendMovementSpeedAck(MovementType type, uint32 ackId, float speed, const MovementInfo& movementInfo);

		void SendMoveTeleportAck(uint32 ackId, const MovementInfo& movementInfo);

		void AutoStoreLootItem(uint8 lootSlot);

		void AutoEquipItem(uint8 srcBag, uint8 srcSlot);

		void AutoStoreBagItem(uint8 srcBag, uint8 srcSlot, uint8 dstBag);

		void SwapItem(uint8 srcBag, uint8 srcSlot, uint8 dstBag, uint8 dstSlot);

		void SwapInvItem(uint8 srcSlot, uint8 dstSlot);

		void SplitItem(uint8 srcBag, uint8 srcSlot, uint8 dstBag, uint8 dstSlot, uint8 count);

		/// Sends a packet to the server to automatically equip a specific item to a specific destination slot.
		void AutoEquipItemSlot(uint64 itemGUID, uint8 dstSlot);

		/// Sends a packet to the server to destroy a specific item from the players inventory.
		void DestroyItem(uint8 bag, uint8 slot, uint8 count);

		/// Sends a packet to the server to start looting a specific game object.
		///	@param lootObjectGuid The guid of the game object we want to loot from.
		void Loot(uint64 lootObjectGuid);

		/// Sends a packet to the server to loot money from the currently looted object.
		void LootMoney();

		/// Sends a loot release packet to the server, notifying it about the player's intention to release the loot.
		///	@param lootObjectGuid The guid of the previously looted object.
		void LootRelease(uint64 lootObjectGuid);

		/// Sends a generic gossip dialog request packet to the server for a specific npc.
		///	@param targetGuid The guid of the npc.
		void GossipHello(uint64 targetGuid);

		/// Sends the packet to load a quest givers quest menu.
		///	@param targetGuid The guid of the quest giver npc.
		void QuestGiverHello(uint64 targetGuid);

		/// Sends the packet to load the trainer menu for a specific npc.
		///	@param targetGuid The guid of the trainer npc.
		void TrainerMenu(uint64 targetGuid);

		/// Sends the packet to list an npcs inventory.
		///	@param targetGuid The guid of the npc.
		void ListInventory(uint64 targetGuid);

		/// Sends a packet to the server indiciating that the player wants to sell an item to a specific vendor. The vendor must be the current vendor, initiated by
		///	either the ListInventory or GossipHello packet.
		///	@param vendorGuid The guid of the vendor npc.
		///	@param itemGuid The guid of the item to sell.
		void SellItem(uint64 vendorGuid, uint64 itemGuid);

		/// Sends a packet to the server indicating that the player wants to buy an item from a specific vendor npc. The vendor must be the current vendor, initiated by
		///	either the ListInventory or GossipHello packet.
		///	@param vendorGuid The guid of the vendor npc.
		///	@param itemId The id of the item to buy.
		///	@param count The amount of stacks to buy (some items can only be bought in stacks of lets say 5, so in this case a value of 2 would actually buy 10 items).
		void BuyItem(uint64 vendorGuid, uint32 itemId, uint8 count);

		/// Sends a packet to the server to spend attribute points on a specific attribute. The amount of attribute points spent is actually determined by the server
		///	and the request will fail if the player does not have enough attribute points to spend.
		///	@param attribute The attribute to spend the point on.
		void AddAttributePoint(uint32 attribute);

		/// Sends a packet to the server indicating that the player wants to use a specific item from the inventory. The item must be in the inventory at the specific
		///	slot and it must be usable. Also, a target map must provide the necessary targets for the item's spell's target requirements.
		///	@param srcBag The bag the item is in.
		///	@param srcSlot The slot the item is in.
		///	@param itemGuid The guid of the item to use.
		///	@param targetMap A map of targets for the item's spell. Must be properly filled to fulfill the item's spell's target requirements.
		void UseItem(uint8 srcBag, uint8 srcSlot, uint64 itemGuid, const SpellTargetMap& targetMap);

		/// Sends a packet to the server which assigns a specific action to an action button on the player's action bar, so that it will be persisted when the player
		///	logs out and back in.
		///	@param index The index of the action button to assign the action to.
		///	@param button The action button to assign to the action bar.
		void SetActionBarButton(uint8 index, const ActionButton& button);

		/// Sends a packet to the server indicating that the current cast or channel should be cancelled. The player needs to be casting or channeling a spell
		///	for this to work, otherwise the server will ignore the request.
		void CancelCast();

		/// Sends a packet to the server indicating that the player wants to cancel an active aura on his character. The aura must exist and be removable for this
		///	to work, otherwise the server will ignore the request.
		void CancelAura();

		/// Sends a packet to the server indicating that the player wants to buy a spell from a trainer npc. The spell must be available for the player to buy and
		///	the player must fulfill all requirements such as level and money. The trainer must be the current trainer, initiated by the TrainerMenu or the GossipHello packet.
		///	@param trainerGuid The guid of the trainer npc.
		///	@param spellId The id of the spell to buy.
		void TrainerBuySpell(uint64 trainerGuid, uint32 spellId);

		/// Sends a packet to the server to ask for an npcs quest giver status update. The response will tell the client what quest giver icon to display above
		///	the npc's head. The npc must have the npc_flags::QuestGiver flag set in the object_fields::NpcFlags object field for this to work and must exist in the
		///	world of the currently controlled player character.
		///	@param questGiverGuid The guid of the npc to update the quest status for.
		void UpdateQuestStatus(uint64 questGiverGuid);

		/// Sends a packet to the server to accept a quest from a specified quest giver object.
		///	@param questGiverGuid The guid quest giver object. Must be a valid object guid.
		/// @param questId The id of the quest to accept. Must be a valid quest id offered by the quest giver object.
		void AcceptQuest(uint64 questGiverGuid, uint32 questId);

		/// Sends a packet to the server to ask for quest details from a quest giver object for a specific quest offered by that quest giver.
		///	@param questGiverGuid The guid of the quest giver object. Must be a valid object guid.
		///	@param questId The id of the quest to query details for. Must be a valid quest id offered by the quest giver object.
		void QuestGiverQueryQuest(uint64 questGiverGuid, uint32 questId);

		/// Sends a packet to the server to ask for quest completion from a quest giver object for a specific quest offered by that quest giver.
		///	@param questGiverGuid The guid of the quest giver object. Must be a valid object guid.
		///	@param questId The id of the quest to query details for. Must be a valid quest id offered by the quest giver object.
		void QuestGiverCompleteQuest(uint64 questGiverGuid, uint32 questId);

		/// Sends a packet to the server to abandon a specific quest. The quest must be in the player's quest log which means he must have accepted it
		///	and not been rewarded for it yet.
		///	@param questId The id of the quest to abandon.
		void AbandonQuest(uint32 questId);

		/// Sends a packet to the server to ask for quest completion from a quest giver object for a specific quest offered by that quest giver.
		///	@param questGiverGuid The guid of the quest giver object. Must be a valid object guid.
		///	@param questId The id of the quest to query details for. Must be a valid quest id offered by the quest giver object.
		void QuestGiverChooseQuestReward(uint64 questGiverGuid, uint32 questId, uint32 rewardChoice);

		/// Gets the id of the realm.
		uint32 GetRealmId() const { return m_realmId; }

	public:
		/// Gets a constant list of character views.
		const std::vector<CharacterView>& GetCharacterViews() const { return m_characterViews; }

	private:
		/// A list of character views.
		std::vector<CharacterView> m_characterViews;
	};
}

