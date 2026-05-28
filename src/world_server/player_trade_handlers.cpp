// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player.h"
#include "player_manager.h"
#include "trade_session.h"

#include "game_server/objects/game_player_s.h"
#include "game_server/inventory.h"
#include "game_server/inventory_types.h"
#include "game/loot.h"
#include "game/chat_type.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static void SendTradeRequestResult(Player& player, game::trade_result::Type result)
	{
		player.SendPacket([result](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::TradeRequestResult);
			packet << io::write<uint8>(static_cast<uint8>(result));
			packet.Finish();
		});
	}

	static void SendTradeSessionOpened(Player& player, const std::string& otherName, uint64 otherGuid)
	{
		player.SendPacket([&otherName, otherGuid](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::TradeSessionOpened);
			packet << io::write_string(otherName);
			packet << io::write<uint64>(otherGuid);
			packet.Finish();
		});
	}

	static void SendTradeSessionClosed(Player& player, uint8 reason)
	{
		player.SendPacket([reason](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::TradeSessionClosed);
			packet << io::write<uint8>(reason);
			packet.Finish();
		});
	}

	void Player::SetTradeSession(std::shared_ptr<TradeSession> session)
	{
		m_tradeSession = std::move(session);
	}

	void Player::CancelTradeSession(const uint8 reason)
	{
		if (!m_tradeSession)
		{
			return;
		}

		const int myIndex = m_tradeSession->GetPlayerIndex(*this);
		if (myIndex < 0)
		{
			m_tradeSession = nullptr;
			return;
		}

		const int otherIndex = m_tradeSession->GetOtherIndex(myIndex);
		Player& other = m_tradeSession->GetPlayer(otherIndex);

		SendTradeSessionClosed(*this, reason);
		SendTradeSessionClosed(other, reason);

		other.SetTradeSession(nullptr);
		m_tradeSession = nullptr;
	}

	void Player::OnTradeInitiate(uint16 /*opCode*/, uint32 /*size*/, io::Reader& contentReader)
	{
		uint64 targetGuid;
		if (!(contentReader >> io::read<uint64>(targetGuid)))
		{
			WLOG("Failed to read target GUID from TradeInitiate packet");
			return;
		}

		// Already in a session or has pending invite
		if (m_tradeSession)
		{
			SendTradeRequestResult(*this, game::trade_result::AlreadyTrading);
			return;
		}

		if (!m_character->IsAlive())
		{
			SendTradeRequestResult(*this, game::trade_result::YouAreDead);
			return;
		}

		if (m_character->IsInCombat())
		{
			SendTradeRequestResult(*this, game::trade_result::YouAreInCombat);
			return;
		}

		const auto targetPtr = m_manager.GetPlayerByCharacterGuid(targetGuid);
		Player* target = targetPtr.get();
		if (!target || target == this)
		{
			SendTradeRequestResult(*this, game::trade_result::PlayerNotFound);
			return;
		}

		if (!target->GetCharacter().IsAlive())
		{
			SendTradeRequestResult(*this, game::trade_result::TargetIsDead);
			return;
		}

		if (target->GetCharacter().IsInCombat())
		{
			SendTradeRequestResult(*this, game::trade_result::TargetIsInCombat);
			return;
		}

		if (target->IsTrading() || target->GetTradeInviterGuid() != 0)
		{
			SendTradeRequestResult(*this, game::trade_result::TargetBusy);
			return;
		}

		const float distanceSq = m_character->GetSquaredDistanceTo(target->GetCharacter().GetPosition(), true);
		if (distanceSq > LootDistance * LootDistance)
		{
			SendTradeRequestResult(*this, game::trade_result::TooFarAway);
			return;
		}

		if (!m_character->UnitIsFriendly(*target->m_character))
		{
			SendTradeRequestResult(*this, game::trade_result::Hostile);
			return;
		}

		// Store pending invite on both sides
		m_tradeInviterGuid = m_character->GetGuid();  // We are the initiator, mark ourselves
		target->SetTradeInviterGuid(m_character->GetGuid());

		// Notify target of the pending invitation
		const std::string& initiatorName = m_characterData.name;
		const uint64 initiatorGuid = m_character->GetGuid();
		target->SendPacket([&initiatorName, initiatorGuid](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::TradeInvite);
			packet << io::write_string(initiatorName);
			packet << io::write<uint64>(initiatorGuid);
			packet.Finish();
		});

		// Confirm to initiator that the request was sent
		SendTradeRequestResult(*this, game::trade_result::Success);

		// Show chat message for initiator
		const std::string& targetName = target->m_characterData.name;
		SendPacket([&targetName](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::ChatMessage);
			packet << io::write<uint8>(static_cast<uint8>(ChatType::System));
			packet << io::write<uint64>(0);
			packet << io::write_string("You want to trade with " + targetName + ".");
			packet.Finish();
		});
	}

	void Player::OnTradeInviteAccept(uint16 /*opCode*/, uint32 /*size*/, io::Reader& /*contentReader*/)
	{
		// We must have a pending invite
		const uint64 inviterGuid = m_tradeInviterGuid;
		if (inviterGuid == 0)
		{
			return;
		}

		// Clear pending invite
		m_tradeInviterGuid = 0;

		// Find the initiator
		const auto initiatorPtr = m_manager.GetPlayerByCharacterGuid(inviterGuid);
		Player* initiator = initiatorPtr.get();
		if (!initiator)
		{
			return;
		}

		// Clear initiator's self-marker
		initiator->m_tradeInviterGuid = 0;

		// Re-validate both players
		if (m_tradeSession || initiator->IsTrading())
		{
			return;
		}

		if (!m_character->IsAlive() || !initiator->GetCharacter().IsAlive())
		{
			return;
		}

		if (m_character->IsInCombat() || initiator->GetCharacter().IsInCombat())
		{
			return;
		}

		const float distanceSq = m_character->GetSquaredDistanceTo(initiator->GetCharacter().GetPosition(), true);
		if (distanceSq > LootDistance * LootDistance)
		{
			SendTradeRequestResult(*initiator, game::trade_result::TooFarAway);
			return;
		}

		if (!m_character->UnitIsFriendly(*initiator->m_character))
		{
			return;
		}

		// Create the trade session
		auto session = std::make_shared<TradeSession>(*initiator, *this);
		initiator->SetTradeSession(session);
		SetTradeSession(session);

		// Notify both players that the session is open
		SendTradeSessionOpened(*initiator, m_characterData.name, m_character->GetGuid());
		SendTradeSessionOpened(*this, initiator->m_characterData.name, initiator->m_character->GetGuid());
	}

	void Player::OnTradeInviteDecline(uint16 /*opCode*/, uint32 /*size*/, io::Reader& /*contentReader*/)
	{
		const uint64 inviterGuid = m_tradeInviterGuid;
		if (inviterGuid == 0)
		{
			return;
		}

		m_tradeInviterGuid = 0;

		const auto initiatorPtr = m_manager.GetPlayerByCharacterGuid(inviterGuid);
		if (Player* initiator = initiatorPtr.get())
		{
			initiator->m_tradeInviterGuid = 0;
			SendTradeRequestResult(*initiator, game::trade_result::Declined);
		}
	}

	void Player::OnTradeCancelRequest(uint16 /*opCode*/, uint32 /*size*/, io::Reader& /*contentReader*/)
	{
		if (!m_tradeSession)
		{
			// Clear a pending invite if we sent one (as initiator)
			if (m_tradeInviterGuid != 0)
			{
				m_tradeInviterGuid = 0;
			}
			return;
		}

		CancelTradeSession(static_cast<uint8>(game::trade_close_reason::Cancelled));
	}

	void Player::OnTradeAddItem(uint16 /*opCode*/, uint32 /*size*/, io::Reader& contentReader)
	{
		uint8 tradeSlot;
		uint16 inventorySlot;

		if (!(contentReader >> io::read<uint8>(tradeSlot) >> io::read<uint16>(inventorySlot)))
		{
			WLOG("Failed to read TradeAddItem packet");
			return;
		}

		if (!m_tradeSession)
		{
			WLOG("Player tried to add trade item without an active session");
			return;
		}

		const int myIndex = m_tradeSession->GetPlayerIndex(*this);
		if (myIndex < 0)
		{
			return;
		}

		if (tradeSlot >= TradeSlotCount)
		{
			WLOG("Trade slot out of range: " << static_cast<uint32>(tradeSlot));
			return;
		}

		const auto item = m_character->GetInventory().GetItemAtSlot(inventorySlot);
		if (!item)
		{
			WLOG("No item at inventory slot " << inventorySlot);
			return;
		}

		// Disallow equip slots and bag slots
		const InventorySlot slot = InventorySlot::FromAbsolute(inventorySlot);
		if (slot.IsEquipment() || slot.IsBagPack())
		{
			WLOG("Player tried to trade an equipped item or bag");
			return;
		}

		m_tradeSession->AddItem(myIndex, tradeSlot, inventorySlot);
	}

	void Player::OnTradeRemoveItem(uint16 /*opCode*/, uint32 /*size*/, io::Reader& contentReader)
	{
		uint8 tradeSlot;
		if (!(contentReader >> io::read<uint8>(tradeSlot)))
		{
			WLOG("Failed to read TradeRemoveItem packet");
			return;
		}

		if (!m_tradeSession)
		{
			return;
		}

		const int myIndex = m_tradeSession->GetPlayerIndex(*this);
		if (myIndex < 0)
		{
			return;
		}

		m_tradeSession->RemoveItem(myIndex, tradeSlot);
	}

	void Player::OnTradeSetMoney(uint16 /*opCode*/, uint32 /*size*/, io::Reader& contentReader)
	{
		uint32 amount;
		if (!(contentReader >> io::read<uint32>(amount)))
		{
			WLOG("Failed to read TradeSetMoney packet");
			return;
		}

		if (!m_tradeSession)
		{
			return;
		}

		const int myIndex = m_tradeSession->GetPlayerIndex(*this);
		if (myIndex < 0)
		{
			return;
		}

		if (!m_character->HasMoney(amount))
		{
			WLOG("Player tried to trade more money than they have");
			return;
		}

		m_tradeSession->SetMoney(myIndex, amount);
	}

	void Player::OnTradeAccept(uint16 /*opCode*/, uint32 /*size*/, io::Reader& /*contentReader*/)
	{
		if (!m_tradeSession)
		{
			return;
		}

		const int myIndex = m_tradeSession->GetPlayerIndex(*this);
		if (myIndex < 0)
		{
			return;
		}

		// Re-check distance
		const int otherIndex = m_tradeSession->GetOtherIndex(myIndex);
		const Player& other = m_tradeSession->GetPlayer(otherIndex);
		const float distanceSq = m_character->GetSquaredDistanceTo(other.GetCharacter().GetPosition(), true);
		if (distanceSq > LootDistance * LootDistance)
		{
			CancelTradeSession(static_cast<uint8>(game::trade_close_reason::TooFarAway));
			return;
		}

		if (!m_tradeSession->IsValid())
		{
			CancelTradeSession(static_cast<uint8>(game::trade_close_reason::Death));
			return;
		}

		const bool bothAccepted = m_tradeSession->Accept(myIndex);
		if (!bothAccepted)
		{
			return;
		}

		// Execute the trade
		const bool success = m_tradeSession->Execute();
		const uint8 closeReason = success
			? static_cast<uint8>(game::trade_close_reason::Completed)
			: static_cast<uint8>(game::trade_close_reason::Cancelled);

		// Capture session then clear on both players before sending packets
		auto session = m_tradeSession;
		const int otherIdx = session->GetOtherIndex(session->GetPlayerIndex(*this));
		Player& otherPlayer = session->GetPlayer(otherIdx);

		SendTradeSessionClosed(*this, closeReason);
		SendTradeSessionClosed(otherPlayer, closeReason);

		otherPlayer.SetTradeSession(nullptr);
		m_tradeSession = nullptr;
	}
}
