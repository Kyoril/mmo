// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "data/db_cache.h"
#include "game/mail.h"
#include "net/realm_connector.h"

struct lua_State;

namespace mmo
{
	/// Client side system which handles the mailbox interaction and the characters inbox.
	class MailClient final : public NonCopyable
	{
	public:
		MailClient(RealmConnector& connector, DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& itemCache);
		~MailClient();

	public:
		void Initialize();

		void Shutdown();

		void RegisterScriptFunctions(lua_State* lua);

	public:
		bool HasMailbox() const { return m_mailboxGuid != 0; }

		uint64 GetMailboxGuid() const { return m_mailboxGuid; }

		void CloseMailbox();

		uint32 GetNumMails() const { return static_cast<uint32>(m_mails.size()); }

		const MailInfo* GetMail(int32 index) const;

		const ItemInfo* GetAttachmentItem(int32 mailIndex, int32 attachmentIndex) const;

		/// Sends a new mail with an optional single item attachment (absolute slot or -1).
		void SendMail(const char* recipient, const char* subject, const char* body, uint32 money, int32 itemSlot) const;

		void TakeMoney(int32 mailIndex) const;

		void TakeItem(int32 mailIndex, int32 attachmentIndex) const;

		void DeleteMail(int32 mailIndex) const;

		void MarkRead(int32 mailIndex);

		void RequestMailList() const;

		uint32 GetUnreadMailCount() const { return m_unreadCount; }

	private:
		PacketParseResult OnShowMailbox(game::IncomingPacket& packet);

		PacketParseResult OnMailList(game::IncomingPacket& packet);

		PacketParseResult OnMailSendResult(game::IncomingPacket& packet);

		PacketParseResult OnMailTakeResult(game::IncomingPacket& packet);

		PacketParseResult OnMailNotify(game::IncomingPacket& packet);

	private:
		RealmConnector& m_realmConnector;
		DBCache<ItemInfo, game::client_realm_packet::ItemQuery>& m_itemCache;
		RealmConnector::PacketHandlerHandleContainer m_packetHandlerConnections;

		uint64 m_mailboxGuid = 0;
		std::vector<MailInfo> m_mails;
		std::map<uint32, const ItemInfo*> m_attachmentItemInfos;
		uint32 m_unreadCount = 0;
	};
}
