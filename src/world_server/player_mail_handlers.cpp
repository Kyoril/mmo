// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player.h"

#include "base/utilities.h"
#include "game/loot.h"
#include "game/mail.h"
#include "game_server/objects/game_bag_s.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/objects/game_world_object_s.h"
#include "proto_data/project.h"

namespace mmo
{
	void Player::OnMailboxUsed(const uint64 mailboxGuid)
	{
		m_activeMailboxGuid = mailboxGuid;
		SendShowMailbox(mailboxGuid);
	}

	bool Player::IsMailboxAccessible() const
	{
		// Must match the range used to open the mailbox (client right-click and OnUseObject)
		constexpr float interactionDistance = LootDistance;

		if (m_activeMailboxGuid == 0 || !m_character->GetWorldInstance())
		{
			return false;
		}

		const GameWorldObjectS* mailbox = m_character->GetWorldInstance()->FindByGuid<GameWorldObjectS>(m_activeMailboxGuid);
		if (!mailbox || !m_character->IsAlive())
		{
			return false;
		}

		return m_character->GetSquaredDistanceTo(mailbox->GetPosition(), true) <= interactionDistance * interactionDistance;
	}

	void Player::OnSendMail(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		String recipient, subject, body;
		uint32 money;
		uint8 slotCount;
		if (!(contentReader
			>> io::read_container<uint8>(recipient)
			>> io::read_container<uint8>(subject)
			>> io::read_container<uint16>(body)
			>> io::read<uint32>(money)
			>> io::read<uint8>(slotCount)))
		{
			ELOG("Failed to read SendMail packet!");
			return;
		}

		std::vector<uint16> itemSlots;
		itemSlots.reserve(slotCount);
		for (uint8 i = 0; i < slotCount; ++i)
		{
			uint16 slot;
			if (!(contentReader >> io::read<uint16>(slot)))
			{
				ELOG("Failed to read SendMail packet!");
				return;
			}

			itemSlots.push_back(slot);
		}

		if (!IsMailboxAccessible())
		{
			m_activeMailboxGuid = 0;
			SendMailSendResult(mail_result::NoMailbox);
			return;
		}

		if (m_pendingMail.active)
		{
			// A previous mail is still awaiting its result
			SendMailSendResult(mail_result::InternalError);
			return;
		}

		if (recipient.empty() || subject.size() > mail::MaxSubjectLength || body.size() > mail::MaxBodyLength)
		{
			SendMailSendResult(mail_result::InternalError);
			return;
		}

		if (_stricmp(recipient.c_str(), m_characterData.name.c_str()) == 0)
		{
			SendMailSendResult(mail_result::CannotSendToSelf);
			return;
		}

		if (itemSlots.size() > mail::MaxAttachments)
		{
			SendMailSendResult(mail_result::TooManyAttachments);
			return;
		}

		const uint32 totalCost = money + mail::Postage;
		if (totalCost < money /* overflow */ || m_character->Get<uint32>(object_fields::Money) < totalCost)
		{
			SendMailSendResult(mail_result::NotEnoughMoney);
			return;
		}

		// Validate all attachments before anything is removed
		Inventory& inventory = m_character->GetInventory();
		std::vector<std::shared_ptr<GameItemS>> items;
		items.reserve(itemSlots.size());
		for (const uint16 slot : itemSlots)
		{
			auto item = inventory.GetItemAtSlot(slot);
			if (!item || IsInventorySlotTradeLocked(slot))
			{
				SendMailSendResult(mail_result::InternalError);
				return;
			}

			if ((item->Get<uint32>(object_fields::ItemFlags) & item_flags::Bound) != 0)
			{
				SendMailSendResult(mail_result::CannotMailBoundItems);
				return;
			}

			if (item->IsContainer() && !std::static_pointer_cast<GameBagS>(item)->IsEmpty())
			{
				SendMailSendResult(mail_result::InternalError);
				return;
			}

			items.push_back(item);
		}

		// Escrow: snapshot and remove the items, take the money
		MailDraft draft;
		draft.senderGuid = m_characterData.characterId;
		draft.senderName = m_characterData.name;
		draft.recipientName = recipient;
		draft.subject = subject;
		draft.body = body;
		draft.money = money;

		for (size_t i = 0; i < items.size(); ++i)
		{
			MailAttachment attachment;
			attachment.entry = items[i]->GetEntry().id();
			attachment.stackCount = static_cast<uint8>(items[i]->GetStackCount());
			attachment.durability = static_cast<uint16>(items[i]->Get<uint32>(object_fields::Durability));
			attachment.creator = items[i]->Get<uint64>(object_fields::Creator);
			attachment.flags = items[i]->Get<uint32>(object_fields::ItemFlags);
			draft.attachments.push_back(attachment);

			if (inventory.RemoveItem(itemSlots[i]) != inventory_change_failure::Okay)
			{
				// Roll back already removed items
				for (size_t j = 0; j < i; ++j)
				{
					if (const auto* entry = m_project.items.getById(draft.attachments[j].entry))
					{
						inventory.CreateItems(*entry, draft.attachments[j].stackCount);
					}
				}

				SendMailSendResult(mail_result::InternalError);
				return;
			}
		}

		m_character->Set<uint32>(object_fields::Money, m_character->Get<uint32>(object_fields::Money) - totalCost);

		m_pendingMail.active = true;
		m_pendingMail.escrowedMoney = totalCost;
		m_pendingMail.attachments = draft.attachments;

		m_connector.SendMailDraft(draft);
	}

	void Player::OnMailDraftResult(const uint8 result)
	{
		if (!m_pendingMail.active)
		{
			return;
		}

		if (result != mail_result::Ok)
		{
			// Refund the escrowed money (minus nothing - postage is only paid on success)
			m_character->Set<uint32>(object_fields::Money, m_character->Get<uint32>(object_fields::Money) + m_pendingMail.escrowedMoney);

			// Refund the escrowed items
			for (const auto& attachment : m_pendingMail.attachments)
			{
				if (const auto* entry = m_project.items.getById(attachment.entry))
				{
					m_character->GetInventory().CreateItems(*entry, attachment.stackCount);
				}
			}
		}

		m_pendingMail = PendingMail{};
		SendMailSendResult(result);
	}

	void Player::OnMailTakeMoney(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 mailId;
		if (!(contentReader >> io::read<uint64>(mailId)))
		{
			ELOG("Failed to read MailTakeMoney packet!");
			return;
		}

		if (!IsMailboxAccessible())
		{
			m_activeMailboxGuid = 0;
			SendMailTakeResult(mail_result::NoMailbox);
			return;
		}

		m_connector.SendMailTakeMoney(m_characterData.characterId, mailId);
	}

	void Player::OnMailTakeMoneyResult(const uint64 mailId, const uint8 result, const uint32 money)
	{
		if (result != mail_result::Ok)
		{
			SendMailTakeResult(result);
			return;
		}

		m_character->Set<uint32>(object_fields::Money, m_character->Get<uint32>(object_fields::Money) + money);
		SendMailTakeResult(mail_result::Ok);
	}

	void Player::OnMailTakeItem(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint64 mailId, attachmentId;
		if (!(contentReader >> io::read<uint64>(mailId) >> io::read<uint64>(attachmentId)))
		{
			ELOG("Failed to read MailTakeItem packet!");
			return;
		}

		if (!IsMailboxAccessible())
		{
			m_activeMailboxGuid = 0;
			SendMailTakeResult(mail_result::NoMailbox);
			return;
		}

		m_connector.SendMailTakeItem(m_characterData.characterId, mailId, attachmentId);
	}

	void Player::OnMailTakeItemResult(const uint64 mailId, const uint8 result, const MailAttachment& attachment)
	{
		if (result != mail_result::Ok)
		{
			SendMailTakeResult(result);
			return;
		}

		const auto* entry = m_project.items.getById(attachment.entry);
		if (!entry ||
			m_character->GetInventory().CreateItems(*entry, attachment.stackCount) != inventory_change_failure::Okay)
		{
			// Give the item back to the mail so it isn't lost
			m_connector.SendMailRestoreItem(mailId, attachment);
			SendMailTakeResult(mail_result::InventoryFull);
			return;
		}

		SendMailTakeResult(mail_result::Ok);
	}

	void Player::SendShowMailbox(const uint64 mailboxGuid)
	{
		SendPacket([mailboxGuid](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::ShowMailbox);
				packet << io::write<uint64>(mailboxGuid);
				packet.Finish();
			});
	}

	void Player::SendMailSendResult(const uint8 result)
	{
		SendPacket([result](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::MailSendResult);
				packet << io::write<uint8>(result);
				packet.Finish();
			});
	}

	void Player::SendMailTakeResult(const uint8 result)
	{
		SendPacket([result](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::MailTakeResult);
				packet << io::write<uint8>(result);
				packet.Finish();
			});
	}
}
