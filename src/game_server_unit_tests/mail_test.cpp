// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/mail.h"

#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("MailInfo - Serialization round trip", "[mail]")
{
	MailInfo mail;
	mail.mailId = 42;
	mail.senderName = "Griphook";
	mail.subject = "Your goods";
	mail.body = "As requested.";
	mail.money = 12345;
	mail.codAmount = 0;
	mail.sentAt = 1700000000;
	mail.expiresAt = 1700000000 + mail::ExpirySeconds;
	mail.read = false;

	MailAttachment attachment;
	attachment.id = 7;
	attachment.entry = 17;
	attachment.stackCount = 5;
	attachment.durability = 40;
	attachment.creator = 0x1234567890ABCDEFULL;
	attachment.flags = 2;
	mail.attachments.push_back(attachment);

	std::vector<char> buffer;
	io::VectorSink sink(buffer);
	io::Writer writer(sink);
	writer << mail;

	io::MemorySource source(buffer.data(), buffer.data() + buffer.size());
	io::Reader reader(source);

	MailInfo restored;
	REQUIRE(reader >> restored);

	REQUIRE(restored.mailId == mail.mailId);
	REQUIRE(restored.senderName == mail.senderName);
	REQUIRE(restored.subject == mail.subject);
	REQUIRE(restored.body == mail.body);
	REQUIRE(restored.money == mail.money);
	REQUIRE(restored.codAmount == mail.codAmount);
	REQUIRE(restored.sentAt == mail.sentAt);
	REQUIRE(restored.expiresAt == mail.expiresAt);
	REQUIRE(restored.read == mail.read);
	REQUIRE(restored.attachments.size() == 1);
	REQUIRE(restored.attachments[0].id == attachment.id);
	REQUIRE(restored.attachments[0].entry == attachment.entry);
	REQUIRE(restored.attachments[0].stackCount == attachment.stackCount);
	REQUIRE(restored.attachments[0].durability == attachment.durability);
	REQUIRE(restored.attachments[0].creator == attachment.creator);
	REQUIRE(restored.attachments[0].flags == attachment.flags);
}

TEST_CASE("Mail - Constants", "[mail]")
{
	SECTION("Postage is a positive copper amount")
	{
		REQUIRE(mail::Postage > 0);
	}

	SECTION("Expiry is 30 days")
	{
		REQUIRE(mail::ExpirySeconds == 30ull * 24 * 60 * 60);
	}

	SECTION("Attachment count is limited")
	{
		REQUIRE(mail::MaxAttachments >= 1);
		REQUIRE(mail::MaxAttachments <= 16);
	}
}
