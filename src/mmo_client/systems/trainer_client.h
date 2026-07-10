#pragma once

#include "connection.h"
#include "base/non_copyable.h"
#include "client_data/project.h"
#include "game_protocol/game_incoming_packet.h"
#include "net/realm_connector.h"

namespace mmo
{
	class TrainerClient final : public NonCopyable
	{
	public:
		struct TrainerSpellEntry
		{
			const proto_client::SpellEntry* spell;
			uint32 cost;
			uint32 requiredLevel;
			uint32 skill;
			uint32 skillValue;
			bool isKnown = false;
		};

	public:
		TrainerClient(RealmConnector& connector, const proto_client::SpellManager& spells);
		~TrainerClient() override;

	public:
		void Initialize();

		void Shutdown();

	public:
		[[nodiscard]] bool HasTrainer() const { return m_trainerGuid != 0; }

		[[nodiscard]] uint64 GetTrainerGuid() const { return m_trainerGuid; }

		void CloseTrainer();

		[[nodiscard]] uint32 GetNumTrainerSpells() const;

		[[nodiscard]] const std::vector<TrainerSpellEntry>& GetTrainerSpells() const { return m_trainerSpells; }

		/// @brief Returns the trainer entry's title string (e.g. "Class Trainer", "Riding Instructor").
		/// @return The trainer title, or empty string if not set.
		[[nodiscard]] const String& GetTrainerTitle() const { return m_trainerTitle; }

		/// @brief Returns the trainer type (see proto TrainerEntry::TrainerType; 0 = class trainer).
		[[nodiscard]] uint8 GetTrainerType() const { return m_trainerType; }

		/// @brief Returns true if the current trainer is a class trainer. Class trainers gate their
		/// spells by the player's CLASS level instead of the character level.
		[[nodiscard]] bool IsClassTrainer() const { return m_trainerType == 0; }

		void BuySpell(uint32 index) const;

		void OnSpellLearned(uint32 spellId);

	private:
		PacketParseResult OnTrainerList(game::IncomingPacket& packet);

		PacketParseResult OnTrainerBuyError(game::IncomingPacket& packet);

		PacketParseResult OnTrainerBuySucceeded(game::IncomingPacket& packet);

	private:
		RealmConnector& m_realmConnector;
		const proto_client::SpellManager& m_spells;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerConnections;

		std::vector<TrainerSpellEntry> m_trainerSpells;
		uint64 m_trainerGuid = 0;
		String m_trainerTitle;
		uint8 m_trainerType = 0;
	};
}
