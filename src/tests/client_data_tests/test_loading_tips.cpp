// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shared/client_data/proto_client/loading_tips.pb.h"

#include "base/localization.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("Loading tips round trip with their translations", "[client_data][loading_tips]")
{
	proto_client::LoadingTips source;
	auto* tip = source.add_entry();
	tip->set_id(1);
	tip->set_name("Resting");
	tip->set_text("Sit down to regenerate faster.");
	auto* loc = tip->add_text_loc();
	loc->set_locale(static_cast<uint32>(LocaleIndex::deDE));
	loc->set_value("Setz dich hin, um schneller zu regenerieren.");

	proto_client::LoadingTips parsed;
	REQUIRE(parsed.ParseFromString(source.SerializeAsString()));
	REQUIRE(parsed.entry_size() == 1);

	const auto& entry = parsed.entry(0);
	CHECK(GetLocalizedString(entry.text(), entry.text_loc(), LocaleIndex::enUS) == "Sit down to regenerate faster.");
	CHECK(GetLocalizedString(entry.text(), entry.text_loc(), LocaleIndex::deDE) == "Setz dich hin, um schneller zu regenerieren.");
	CHECK(GetLocalizedString(entry.text(), entry.text_loc(), LocaleIndex::frFR) == "Sit down to regenerate faster.");
}

TEST_CASE("Loading tip translations use field 200 like the editor schema", "[client_data][loading_tips]")
{
	// ClientDB export copies the editor's bytes, so the client mirror must keep the server's field numbers.
	proto_client::LoadingTip tip;
	tip.set_id(1);
	tip.set_name("x");
	tip.add_text_loc()->set_value("y");

	const std::string bytes = tip.SerializeAsString();

	// Tag of field 200, length delimited: (200 << 3) | 2 = 1602, as varint 0xC2 0x0C.
	CHECK(bytes.find(std::string("\xC2\x0C", 2)) != std::string::npos);
}
