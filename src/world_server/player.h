// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "realm_connector.h"
#include "vector_sink.h"
#include "game/character_data.h"
#include "game/chat_type.h"
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
		class Project;
	}

	/// @brief This class represents the connection to a player on a realm server that this
	///	       world node is connected to.
	class Player final : public TileSubscriber, public NetUnitWatcherS
	{
	public:
		explicit Player(RealmConnector& realmConnector, std::shared_ptr<GamePlayerS> characterObject,
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

		void SaveCharacterData() const;

		void OpenLootDialog(std::shared_ptr<LootInstance> lootInstance, std::shared_ptr<GameObjectS> source);

		void CloseLootDialog();

		bool IsLooting() const;

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

	public:
		/// @brief Gets the character guid.
		[[nodiscard]] uint64 GetCharacterGuid() const noexcept { return m_character->GetGuid(); }

	private:
		void OnSetSelection(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnMovement(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnCheatCreateMonster(uint16 opCode, uint32 size, io::Reader& contentReader) const;

		void OnCheatDestroyMonster(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnCheatLearnSpell(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnSpellCast(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnAttackSwing(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnAttackStop(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnCheatFollowMe(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnCheatFaceMe(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnReviveRequest(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnClientAck(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnAutoStoreLootItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnAutoEquipItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnAutoStoreBagItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnSwapItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnSwapInvItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnSplitItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnAutoEquipItemSlot(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnDestroyItem(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnLoot(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnLootMoney(uint16 opCode, uint32 size, io::Reader& contentReader);

		void OnLootRelease(uint16 opCode, uint32 size, io::Reader& contentReader);

	private:
		void OnSpellLearned(GameUnitS& unit, const proto::SpellEntry& spellEntry);

		void OnSpellUnlearned(GameUnitS& unit, const proto::SpellEntry& spellEntry);

	public:
		void OnAttackSwingEvent(AttackSwingEvent attackSwingEvent) override;

		void OnXpLog(uint32 amount) override;

		void OnSpellDamageLog(uint64 targetGuid, uint32 amount, uint8 school, DamageFlags flags, const proto::SpellEntry& spell) override;

		void OnNonSpellDamageLog(uint64 targetGuid, uint32 amount, DamageFlags flags) override;

		void OnSpeedChangeApplied(MovementType type, float speed, uint32 ackId) override;

	private:
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
