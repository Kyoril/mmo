// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mail_client.h"

#include "frame_ui/frame_mgr.h"
#include "game_client/object_mgr.h"

#include "luabind_lambda.h"

namespace mmo
{
	MailClient::MailClient(RealmConnector& connector, DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& itemCache)
		: m_realmConnector(connector)
		, m_itemCache(itemCache)
	{
	}

	MailClient::~MailClient()
		= default;

	void MailClient::Initialize()
	{
		ASSERT(m_packetHandlerConnections.IsEmpty());

		m_mailboxGuid = 0;
		m_mails.clear();
		m_attachmentItemInfos.clear();
		m_unreadCount = 0;

		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::ShowMailbox, *this, &MailClient::OnShowMailbox);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MailList, *this, &MailClient::OnMailList);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MailSendResult, *this, &MailClient::OnMailSendResult);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MailTakeResult, *this, &MailClient::OnMailTakeResult);
		m_packetHandlerConnections += m_realmConnector.RegisterAutoPacketHandler(game::realm_client_packet::MailNotify, *this, &MailClient::OnMailNotify);
	}

	void MailClient::Shutdown()
	{
		m_packetHandlerConnections.Clear();
	}

	void MailClient::RegisterScriptFunctions(lua_State* lua)
	{
		LUABIND_MODULE(lua,
			luabind::class_<MailInfo>("MailInfo")
				.def_readonly("senderName", &MailInfo::senderName)
				.def_readonly("subject", &MailInfo::subject)
				.def_readonly("body", &MailInfo::body)
				.def_readonly("money", &MailInfo::money)
				.def_readonly("read", &MailInfo::read),

			luabind::def_lambda("CloseMailbox", [this]() { CloseMailbox(); }),
			luabind::def_lambda("RequestMailList", [this]() { RequestMailList(); }),
			luabind::def_lambda("GetNumMails", [this]() { return GetNumMails(); }),
			luabind::def_lambda("GetMailInfo", [this](int32 index) { return GetMail(index); }),
			luabind::def_lambda("GetMailNumAttachments", [this](int32 index) -> uint32
				{
					const MailInfo* mail = GetMail(index);
					return mail ? static_cast<uint32>(mail->attachments.size()) : 0;
				}),
			luabind::def_lambda("GetMailAttachmentItem", [this](int32 mailIndex, int32 attachmentIndex) { return GetAttachmentItem(mailIndex, attachmentIndex); }),
			luabind::def_lambda("GetMailAttachmentCount", [this](int32 mailIndex, int32 attachmentIndex) -> uint32
				{
					const MailInfo* mail = GetMail(mailIndex);
					if (!mail || attachmentIndex < 0 || attachmentIndex >= static_cast<int32>(mail->attachments.size()))
					{
						return 0;
					}
					return mail->attachments[attachmentIndex].stackCount;
				}),
			luabind::def_lambda("SendMail", [this](const char* recipient, const char* subject, const char* body, uint32 money, int32 itemSlot)
				{
					SendMail(recipient, subject, body, money, itemSlot);
				}),
			luabind::def_lambda("TakeMailMoney", [this](int32 index) { TakeMoney(index); }),
			luabind::def_lambda("TakeMailItem", [this](int32 mailIndex, int32 attachmentIndex) { TakeItem(mailIndex, attachmentIndex); }),
			luabind::def_lambda("DeleteMail", [this](int32 index) { DeleteMail(index); }),
			luabind::def_lambda("MarkMailRead", [this](int32 index) { MarkRead(index); }),
			luabind::def_lambda("GetUnreadMailCount", [this]() { return GetUnreadMailCount(); })
		);
	}

	const MailInfo* MailClient::GetMail(const int32 index) const
	{
		if (index < 0 || index >= static_cast<int32>(m_mails.size()))
		{
			return nullptr;
		}

		return &m_mails[index];
	}

	const ItemInfo* MailClient::GetAttachmentItem(const int32 mailIndex, const int32 attachmentIndex) const
	{
		const MailInfo* mail = GetMail(mailIndex);
		if (!mail || attachmentIndex < 0 || attachmentIndex >= static_cast<int32>(mail->attachments.size()))
		{
			return nullptr;
		}

		const auto it = m_attachmentItemInfos.find(mail->attachments[attachmentIndex].entry);
		return it != m_attachmentItemInfos.end() ? it->second : nullptr;
	}

	void MailClient::CloseMailbox()
	{
		if (m_mailboxGuid == 0)
		{
			return;
		}

		m_mailboxGuid = 0;

		FrameManager::Get().TriggerLuaEvent("MAILBOX_CLOSED");
	}

	void MailClient::SendMail(const char* recipient, const char* subject, const char* body, const uint32 money, const int32 itemSlot) const
	{
		if (!m_mailboxGuid)
		{
			ELOG("No mailbox available right now!");
			return;
		}

		std::vector<uint16> itemSlots;
		if (itemSlot >= 0)
		{
			itemSlots.push_back(static_cast<uint16>(itemSlot));
		}

		m_realmConnector.SendMail(recipient, subject, body, money, itemSlots);
	}

	void MailClient::TakeMoney(const int32 mailIndex) const
	{
		if (const MailInfo* mail = GetMail(mailIndex))
		{
			m_realmConnector.MailTakeMoney(mail->mailId);
		}
	}

	void MailClient::TakeItem(const int32 mailIndex, const int32 attachmentIndex) const
	{
		const MailInfo* mail = GetMail(mailIndex);
		if (!mail || attachmentIndex < 0 || attachmentIndex >= static_cast<int32>(mail->attachments.size()))
		{
			return;
		}

		m_realmConnector.MailTakeItem(mail->mailId, mail->attachments[attachmentIndex].id);
	}

	void MailClient::DeleteMail(const int32 mailIndex) const
	{
		if (const MailInfo* mail = GetMail(mailIndex))
		{
			m_realmConnector.MailDelete(mail->mailId);
		}
	}

	void MailClient::MarkRead(const int32 mailIndex)
	{
		if (MailInfo* mail = (mailIndex >= 0 && mailIndex < static_cast<int32>(m_mails.size())) ? &m_mails[mailIndex] : nullptr)
		{
			if (!mail->read)
			{
				mail->read = true;
				if (m_unreadCount > 0)
				{
					m_unreadCount--;
				}

				m_realmConnector.MailMarkRead(mail->mailId);
			}
		}
	}

	void MailClient::RequestMailList() const
	{
		m_realmConnector.MailListRequest();
	}

	PacketParseResult MailClient::OnShowMailbox(game::IncomingPacket& packet)
	{
		uint64 mailboxGuid;
		if (!(packet >> io::read<uint64>(mailboxGuid)))
		{
			ELOG("Failed to read ShowMailbox packet!");
			return PacketParseResult::Disconnect;
		}

		m_mailboxGuid = mailboxGuid;

		// Refresh the inbox; the mail window opens when the list arrives
		RequestMailList();

		FrameManager::Get().TriggerLuaEvent("MAILBOX_SHOW");

		return PacketParseResult::Pass;
	}

	PacketParseResult MailClient::OnMailList(game::IncomingPacket& packet)
	{
		uint16 mailCount;
		if (!(packet >> io::read<uint16>(mailCount)))
		{
			ELOG("Failed to read MailList packet!");
			return PacketParseResult::Disconnect;
		}

		m_mails.clear();
		m_mails.reserve(mailCount);
		m_unreadCount = 0;

		for (uint16 i = 0; i < mailCount; ++i)
		{
			MailInfo mail;
			if (!(packet >> mail))
			{
				ELOG("Failed to read MailList packet!");
				return PacketParseResult::Disconnect;
			}

			if (!mail.read)
			{
				m_unreadCount++;
			}

			m_mails.push_back(std::move(mail));
		}

		// Resolve item info for all attachments so the UI can show icons and names
		for (const auto& mail : m_mails)
		{
			for (const auto& attachment : mail.attachments)
			{
				m_itemCache.Get(attachment.entry, [this](uint64 id, const ItemInfo& itemInfo)
					{
						m_attachmentItemInfos[static_cast<uint32>(id)] = &itemInfo;
						FrameManager::Get().TriggerLuaEvent("MAIL_LIST_UPDATED");
					});
			}
		}

		FrameManager::Get().TriggerLuaEvent("MAIL_LIST_UPDATED");

		return PacketParseResult::Pass;
	}

	PacketParseResult MailClient::OnMailSendResult(game::IncomingPacket& packet)
	{
		uint8 result;
		if (!(packet >> io::read<uint8>(result)))
		{
			ELOG("Failed to read MailSendResult packet!");
			return PacketParseResult::Disconnect;
		}

		switch (result)
		{
		case mail_result::Ok:
			FrameManager::Get().TriggerLuaEvent("MAIL_SENT");
			break;
		case mail_result::RecipientNotFound:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_MAIL_RECIPIENT_NOT_FOUND");
			break;
		case mail_result::NotEnoughMoney:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_MAIL_NOT_ENOUGH_MONEY");
			break;
		case mail_result::CannotSendToSelf:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_MAIL_CANNOT_SEND_TO_SELF");
			break;
		case mail_result::CannotMailBoundItems:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_MAIL_BOUND_ITEM");
			break;
		case mail_result::NoMailbox:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_MAIL_NO_MAILBOX");
			break;
		default:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_MAIL_INTERNAL_ERROR");
			break;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult MailClient::OnMailTakeResult(game::IncomingPacket& packet)
	{
		uint8 result;
		if (!(packet >> io::read<uint8>(result)))
		{
			ELOG("Failed to read MailTakeResult packet!");
			return PacketParseResult::Disconnect;
		}

		switch (result)
		{
		case mail_result::Ok:
			// Refresh the inbox to reflect the taken money/item
			RequestMailList();
			break;
		case mail_result::InventoryFull:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_MAIL_INVENTORY_FULL");
			break;
		case mail_result::NoMailbox:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_MAIL_NO_MAILBOX");
			break;
		default:
			FrameManager::Get().TriggerLuaEvent("GAME_ERROR", "ERR_MAIL_INTERNAL_ERROR");
			break;
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult MailClient::OnMailNotify(game::IncomingPacket& packet)
	{
		uint32 unreadCount;
		if (!(packet >> io::read<uint32>(unreadCount)))
		{
			ELOG("Failed to read MailNotify packet!");
			return PacketParseResult::Disconnect;
		}

		m_unreadCount = unreadCount;

		FrameManager::Get().TriggerLuaEvent("MAIL_NOTIFY");

		return PacketParseResult::Pass;
	}
}
