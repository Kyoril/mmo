
#include "trainer_client.h"

#include "frame_ui/frame_mgr.h"
#include "game/vendor.h"
#include "game_client/game_player_c.h"
#include "game_client/object_mgr.h"

namespace mmo
{
	TrainerClient::TrainerClient(RealmConnector& connector, const proto_client::SpellManager& spells)
		: m_realmConnector(connector)
		, m_spells(spells)
	{
	}

	TrainerClient::~TrainerClient()
		= default;

	void TrainerClient::Initialize()
	{
		ASSERT(m_packetHandlerConnections.IsEmpty());

		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::TrainerList, *this, &TrainerClient::OnTrainerList);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::TrainerBuyError, *this, &TrainerClient::OnTrainerBuyError);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::TrainerBuySucceeded, *this, &TrainerClient::OnTrainerBuySucceeded);
	}

	void TrainerClient::Shutdown()
	{
		m_packetHandlerConnections.Clear();
	}

	void TrainerClient::CloseTrainer()
	{
		if (!HasTrainer())
		{
			return;
		}

		m_trainerGuid = 0;
		m_trainerSpells.clear();

		FrameManager::Get().TriggerLuaEvent("TRAINER_CLOSED");
	}

	uint32 TrainerClient::GetNumTrainerSpells() const
	{
		return m_trainerSpells.size();
	}

	void TrainerClient::BuySpell(uint32 index) const
	{
		if (!HasTrainer())
		{
			ELOG("No trainer available right now!");
			return;
		}

		if (index >= m_trainerSpells.size())
		{
			ELOG("Invalid index to buy from!");
			return;
		}

		// Note: The checks below are also done on the server. However, in order to provide a better user experience by making responses
		// faster and also to protect the server a bit from useless requests we do the checks here on the client side as well. Just remember
		// we can't trust the checks here. The server is the authority.
		const std::shared_ptr<GamePlayerC>& player = ObjectMgr::GetActivePlayer();
		ASSERT(player);

		// Check level
		if (player->GetLevel() < m_trainerSpells[index].requiredLevel)
		{
			FrameManager::Get().TriggerLuaEvent("TRAINER_BUY_ERROR", 0);
			return;
		}

		// Check money
		if (player->Get<uint32>(object_fields::Money) < m_trainerSpells[index].cost)
		{
			FrameManager::Get().TriggerLuaEvent("TRAINER_BUY_ERROR", 1);
			return;
		}

		// TODO: Skill requirements checks

		// Send buy packet
		m_realmConnector.TrainerBuySpell(m_trainerGuid, m_trainerSpells[index].spell->id());
	}

	PacketParseResult TrainerClient::OnTrainerList(game::IncomingPacket& packet)
	{
		uint64 trainerGuid;
		uint16 spellCount;
		if (!(packet >> io::read<uint64>(trainerGuid) >> io::read<uint16>(spellCount)))
		{
			ELOG("Failed to read trainer list packet!");
			return PacketParseResult::Disconnect;
		}

		m_trainerGuid = trainerGuid;
		m_trainerSpells.clear();

		if (spellCount == 0)
		{
			WLOG("Trainer has no spells");
		}

		for (uint16 i = 0; i < spellCount; ++i)
		{
			TrainerSpellEntry trainerSpellEntry;

			uint32 spellId;
			if (!(packet
				>> io::read<uint32>(spellId)
				>> io::read<uint32>(trainerSpellEntry.cost)
				>> io::read<uint32>(trainerSpellEntry.requiredLevel)
				>> io::read<uint32>(trainerSpellEntry.skill)
				>> io::read<uint32>(trainerSpellEntry.skillValue)))
			{
				ELOG("Failed to read trainer list spell entry");
				return PacketParseResult::Disconnect;
			}

			trainerSpellEntry.spell = m_spells.getById(spellId);
			if (trainerSpellEntry.spell == nullptr)
			{
				ELOG("Failed to find spell with id " << spellId);
				return PacketParseResult::Disconnect;
			}

			// Skip spells already known for now
			trainerSpellEntry.isKnown = ObjectMgr::GetActivePlayer()->HasSpell(spellId);
			m_trainerSpells.emplace_back(std::move(trainerSpellEntry));
		}

		// Notify the loot frame manager
		FrameManager::Get().TriggerLuaEvent("TRAINER_SHOW");

		return PacketParseResult::Pass;
	}

	PacketParseResult TrainerClient::OnTrainerBuyError(game::IncomingPacket& packet)
	{
		uint64 trainerGuid;
		uint8 trainerBuyResult;
		if (!(packet >> io::read<uint64>(trainerGuid) >> io::read<uint8>(trainerBuyResult)))
		{
			ELOG("Failed to read trainer buy error packet!");
			return PacketParseResult::Disconnect;
		}

		ASSERT(trainerGuid == m_trainerGuid);

		switch(trainerBuyResult)
		{
		case trainer_result::FailedLevelTooLow:
			FrameManager::Get().TriggerLuaEvent("TRAINER_BUY_ERROR", 0);
			break;

		case trainer_result::FailedNotEnoughMoney:
			FrameManager::Get().TriggerLuaEvent("TRAINER_BUY_ERROR", 1);
			break;

		default:
			ASSERT(false && "Unknown trainer buy result op code received!");
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult TrainerClient::OnTrainerBuySucceeded(game::IncomingPacket& packet)
	{
		uint64 trainerGuid;
		uint32 spellId;
		if (!(packet >> io::read<uint64>(trainerGuid) >> io::read<uint32>(spellId)))
		{
			ELOG("Failed to read trainer buy succeeded packet!");
			return PacketParseResult::Disconnect;
		}

		ASSERT(trainerGuid == m_trainerGuid);

		// Notify the loot frame manager
		FrameManager::Get().TriggerLuaEvent("TRAINER_BUY_SUCCEEDED", spellId);

		return PacketParseResult::Pass;
	}
}
