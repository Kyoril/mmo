// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"

#include <vector>

namespace mmo
{
	namespace mail
	{
		/// Fixed postage cost in copper for sending a mail.
		constexpr uint32 Postage = 30;

		/// Time in seconds until an unread mail expires and is deleted.
		constexpr uint64 ExpirySeconds = 30ull * 24 * 60 * 60;

		/// Maximum number of item attachments a single mail can carry.
		constexpr uint32 MaxAttachments = 12;

		/// Maximum length of a mail subject in characters.
		constexpr uint32 MaxSubjectLength = 64;

		/// Maximum length of a mail body in characters.
		constexpr uint32 MaxBodyLength = 500;
	}

	namespace mail_result
	{
		enum Type
		{
			/// The mail operation succeeded.
			Ok,

			/// The recipient character does not exist.
			RecipientNotFound,

			/// The sender can't afford the money attachment plus postage.
			NotEnoughMoney,

			/// Mails can't be sent to the sending character itself.
			CannotSendToSelf,

			/// One of the attached items is soulbound and can't be mailed.
			CannotMailBoundItems,

			/// Too many items attached to the mail.
			TooManyAttachments,

			/// The mail no longer exists or has nothing left to take.
			MailNotFound,

			/// The recipients inventory is full while taking an item attachment.
			InventoryFull,

			/// The player has no open mailbox interaction.
			NoMailbox,

			/// An internal error occurred (database failure or similar).
			InternalError
		};
	}

	/// A single item attached to a mail.
	struct MailAttachment
	{
		/// Unique attachment id assigned by the database. 0 for unsent drafts.
		uint64 id = 0;
		uint32 entry = 0;
		uint8 stackCount = 1;
		uint16 durability = 0;
		uint64 creator = 0;
		uint32 flags = 0;
	};

	inline io::Writer& operator<<(io::Writer& writer, const MailAttachment& attachment)
	{
		return writer
			<< io::write<uint64>(attachment.id)
			<< io::write<uint32>(attachment.entry)
			<< io::write<uint8>(attachment.stackCount)
			<< io::write<uint16>(attachment.durability)
			<< io::write<uint64>(attachment.creator)
			<< io::write<uint32>(attachment.flags);
	}

	inline io::Reader& operator>>(io::Reader& reader, MailAttachment& attachment)
	{
		return reader
			>> io::read<uint64>(attachment.id)
			>> io::read<uint32>(attachment.entry)
			>> io::read<uint8>(attachment.stackCount)
			>> io::read<uint16>(attachment.durability)
			>> io::read<uint64>(attachment.creator)
			>> io::read<uint32>(attachment.flags);
	}

	/// A mail draft as sent from the world node to the realm for persistence. The world
	/// has already escrowed the attached money and items out of the senders inventory.
	struct MailDraft
	{
		uint64 senderGuid = 0;
		String senderName;
		String recipientName;
		String subject;
		String body;
		uint32 money = 0;
		std::vector<MailAttachment> attachments;
	};

	inline io::Writer& operator<<(io::Writer& writer, const MailDraft& draft)
	{
		writer
			<< io::write<uint64>(draft.senderGuid)
			<< io::write_dynamic_range<uint8>(draft.senderName)
			<< io::write_dynamic_range<uint8>(draft.recipientName)
			<< io::write_dynamic_range<uint8>(draft.subject)
			<< io::write_dynamic_range<uint16>(draft.body)
			<< io::write<uint32>(draft.money)
			<< io::write<uint8>(draft.attachments.size());

		for (const auto& attachment : draft.attachments)
		{
			writer << attachment;
		}

		return writer;
	}

	inline io::Reader& operator>>(io::Reader& reader, MailDraft& draft)
	{
		uint8 attachmentCount = 0;
		if (!(reader
			>> io::read<uint64>(draft.senderGuid)
			>> io::read_container<uint8>(draft.senderName)
			>> io::read_container<uint8>(draft.recipientName)
			>> io::read_container<uint8>(draft.subject)
			>> io::read_container<uint16>(draft.body)
			>> io::read<uint32>(draft.money)
			>> io::read<uint8>(attachmentCount)))
		{
			return reader;
		}

		draft.attachments.clear();
		draft.attachments.reserve(attachmentCount);
		for (uint8 i = 0; i < attachmentCount; ++i)
		{
			MailAttachment attachment;
			if (!(reader >> attachment))
			{
				return reader;
			}

			draft.attachments.push_back(attachment);
		}

		return reader;
	}

	/// Result of persisting a new mail in the database.
	struct MailCreationResult
	{
		mail_result::Type result = mail_result::InternalError;
		uint64 mailId = 0;
		uint64 recipientId = 0;
	};

	/// A mail as presented in a characters inbox.
	struct MailInfo
	{
		uint64 mailId = 0;
		String senderName;
		String subject;
		String body;
		uint32 money = 0;
		uint32 codAmount = 0;
		uint64 sentAt = 0;
		uint64 expiresAt = 0;
		bool read = false;
		std::vector<MailAttachment> attachments;
	};

	inline io::Writer& operator<<(io::Writer& writer, const MailInfo& mail)
	{
		writer
			<< io::write<uint64>(mail.mailId)
			<< io::write_dynamic_range<uint8>(mail.senderName)
			<< io::write_dynamic_range<uint8>(mail.subject)
			<< io::write_dynamic_range<uint16>(mail.body)
			<< io::write<uint32>(mail.money)
			<< io::write<uint32>(mail.codAmount)
			<< io::write<uint64>(mail.sentAt)
			<< io::write<uint64>(mail.expiresAt)
			<< io::write<uint8>(mail.read)
			<< io::write<uint8>(mail.attachments.size());

		for (const auto& attachment : mail.attachments)
		{
			writer << attachment;
		}

		return writer;
	}

	inline io::Reader& operator>>(io::Reader& reader, MailInfo& mail)
	{
		uint8 attachmentCount = 0;
		if (!(reader
			>> io::read<uint64>(mail.mailId)
			>> io::read_container<uint8>(mail.senderName)
			>> io::read_container<uint8>(mail.subject)
			>> io::read_container<uint16>(mail.body)
			>> io::read<uint32>(mail.money)
			>> io::read<uint32>(mail.codAmount)
			>> io::read<uint64>(mail.sentAt)
			>> io::read<uint64>(mail.expiresAt)
			>> io::read<uint8>(mail.read)
			>> io::read<uint8>(attachmentCount)))
		{
			return reader;
		}

		mail.attachments.clear();
		mail.attachments.reserve(attachmentCount);
		for (uint8 i = 0; i < attachmentCount; ++i)
		{
			MailAttachment attachment;
			if (!(reader >> attachment))
			{
				return reader;
			}

			mail.attachments.push_back(attachment);
		}

		return reader;
	}
}
