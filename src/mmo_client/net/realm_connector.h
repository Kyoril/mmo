// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "realm_data.h"

#include "game_protocol/game_connector.h"
#include "base/big_number.h"
#include "base/signal.h"
#include "game/character_view.h"
#include "game/movement_type.h"

#include "asio/io_service.hpp"


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

		signal<void()> Disconnected;

		signal<void(game::player_login_response::Type)> EnterWorldFailed;

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
		/// Handles the LogonChallenge packet.
		PacketParseResult OnAuthChallenge(game::IncomingPacket& packet);

		/// Handles the AuthSessionResponse packet.
		PacketParseResult OnAuthSessionResponse(game::IncomingPacket& packet);

		/// Handles the CharEnum packet.
		PacketParseResult OnCharEnum(game::IncomingPacket& packet);

		PacketParseResult OnLoginVerifyWorld(game::IncomingPacket& packet);

		PacketParseResult OnEnterWorldFailed(game::IncomingPacket& packet);

	public:
		// ~ Begin IConnectorListener
		bool connectionEstablished(bool success) override;
		void connectionLost() override;
		void connectionMalformedPacket() override;
		PacketParseResult connectionPacketReceived(game::IncomingPacket &packet) override;
		// ~ End IConnectorListener

		struct PacketHandlerRegistrationHandle final
		{
		private:
			std::weak_ptr<RealmConnector> m_connector;
			uint16 m_opCode;

		public:
			// Copy operations are deleted to prevent copying
			PacketHandlerRegistrationHandle(const PacketHandlerRegistrationHandle&) = delete;
			PacketHandlerRegistrationHandle& operator=(const PacketHandlerRegistrationHandle&) = delete;

			PacketHandlerRegistrationHandle(RealmConnector& connector, const uint16 opCode)
				: m_connector(std::static_pointer_cast<RealmConnector>(connector.shared_from_this())), m_opCode(opCode)
			{
			}

			PacketHandlerRegistrationHandle(PacketHandlerRegistrationHandle&& other) noexcept
				: m_connector(std::move(other.m_connector)), m_opCode(other.m_opCode)
			{
				other.m_opCode = std::numeric_limits<uint16>::max();
			}

			PacketHandlerRegistrationHandle& operator=(PacketHandlerRegistrationHandle&& other) noexcept
			{
				m_connector = std::move(other.m_connector);
				m_opCode = other.m_opCode;
				other.m_opCode = std::numeric_limits<uint16>::max();
				return *this;
			}

			~PacketHandlerRegistrationHandle()
			{
				const std::shared_ptr<RealmConnector> strongConnector = m_connector.lock();
				if (m_opCode != std::numeric_limits<uint16>::max() && strongConnector)
				{
					strongConnector->ClearPacketHandler(m_opCode);
				}
			}
		};

		struct PacketHandlerHandleContainer final
		{
		private:
			std::vector<PacketHandlerRegistrationHandle> m_handles;

		public:
			void Add(PacketHandlerRegistrationHandle&& handle)
			{
				m_handles.push_back(std::move(handle));
			}

			void Clear()
			{
				m_handles.clear();
			}

			[[nodiscard]] bool IsEmpty() const
			{
				return m_handles.empty();
			}

		public:
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
		/// Sets login data
		void SetLoginData(const std::string& accountName, const BigNumber& sessionKey);

		/// 
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

		/// Gets the realm name.
		const std::string& GetRealmName() const { return m_realmName; }

		void SendMovementUpdate(uint64 characterId, uint16 opCode, const MovementInfo& info);

		void SetSelection(uint64 guid);

		void CreateMonster(uint32 entry);

		void DestroyMonster(uint64 guid);

		void FaceMe(uint64 guid);

		void FollowMe(uint64 guid);

		void LearnSpell(uint32 spellId);

		void LevelUp(uint8 level);

		void GiveMoney(uint32 amount);

		void AddItem(uint32 itemId, uint8 count);

		void CastSpell(uint32 spellId, const SpellTargetMap& targetMap);

		void SendReviveRequest();

		void SendMovementSpeedAck(MovementType type, uint32 ackId, float speed, const MovementInfo& movementInfo);

		void SendMoveTeleportAck(uint32 ackId, const MovementInfo& movementInfo);

		void AutoStoreLootItem(uint8 lootSlot);

		void AutoEquipItem(uint8 srcBag, uint8 srcSlot);

		void AutoStoreBagItem(uint8 srcBag, uint8 srcSlot, uint8 dstBag);

		void SwapItem(uint8 srcBag, uint8 srcSlot, uint8 dstBag, uint8 dstSlot);

		void SwapInvItem(uint8 srcSlot, uint8 dstSlot);

		void SplitItem(uint8 srcBag, uint8 srcSlot, uint8 dstBag, uint8 dstSlot, uint8 count);

		void AutoEquipItemSlot(uint64 itemGUID, uint8 dstSlot);

		void DestroyItem(uint8 bag, uint8 slot, uint8 count);

		void Loot(uint64 lootObjectGuid);

		void LootMoney();

		void LootRelease(uint64 lootObjectGuid);

		void GossipHello(uint64 targetGuid);

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

