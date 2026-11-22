// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shared/client_data/proto_client/cutscenes.pb.h"

#include "base/localization.h"

#include "catch.hpp"

using namespace mmo;

TEST_CASE("Cutscenes round trip with shots, sounds and translated subtitles", "[client_data][cutscenes]")
{
	proto_client::Cutscenes source;
	auto* cutscene = source.add_entry();
	cutscene->set_id(1);
	cutscene->set_name("Intro");
	cutscene->set_map(0);
	cutscene->set_blend_out(3000);
	cutscene->set_music(42);

	auto* shot = cutscene->add_shots();
	shot->set_fade_in(1500);
	auto* keyframe = shot->add_keyframes();
	keyframe->set_time(4000);
	keyframe->set_x(1.0f);
	keyframe->set_y(2.0f);
	keyframe->set_z(3.0f);
	keyframe->set_yaw(90.0f);
	keyframe->set_pitch(-15.0f);

	auto* sound = cutscene->add_sounds();
	sound->set_time(500);
	sound->set_sound(7);

	auto* text = cutscene->add_texts();
	text->set_time(1000);
	text->set_duration(4000);
	text->set_text("Welcome to Alestia.");
	auto* loc = text->add_text_loc();
	loc->set_locale(static_cast<uint32>(LocaleIndex::deDE));
	loc->set_value("Willkommen in Alestia.");

	proto_client::Cutscenes parsed;
	REQUIRE(parsed.ParseFromString(source.SerializeAsString()));
	REQUIRE(parsed.entry_size() == 1);

	const auto& entry = parsed.entry(0);
	CHECK(entry.blend_in() == 0);
	CHECK(entry.blend_out() == 3000);
	CHECK(entry.music() == 42);

	// A cutscene may be skipped unless it says otherwise
	CHECK(entry.flags() == 1);

	REQUIRE(entry.shots_size() == 1);
	CHECK(entry.shots(0).fade_in() == 1500);
	REQUIRE(entry.shots(0).keyframes_size() == 1);
	CHECK(entry.shots(0).keyframes(0).time() == 4000);
	CHECK(entry.shots(0).keyframes(0).yaw() == 90.0f);
	CHECK(entry.shots(0).keyframes(0).fov() == 0.0f);

	REQUIRE(entry.sounds_size() == 1);
	CHECK(entry.sounds(0).sound() == 7);

	REQUIRE(entry.texts_size() == 1);
	const auto& line = entry.texts(0);
	CHECK(GetLocalizedString(line.text(), line.text_loc(), LocaleIndex::enUS) == "Welcome to Alestia.");
	CHECK(GetLocalizedString(line.text(), line.text_loc(), LocaleIndex::deDE) == "Willkommen in Alestia.");
	CHECK(GetLocalizedString(line.text(), line.text_loc(), LocaleIndex::frFR) == "Welcome to Alestia.");
}

TEST_CASE("Cutscene subtitle translations use field 200 like the editor schema", "[client_data][cutscenes]")
{
	// ClientDB export copies the editor's bytes, so the client mirror must keep the server's field numbers.
	proto_client::CutsceneText text;
	text.set_time(0);
	text.set_duration(1);
	text.add_text_loc()->set_value("y");

	const std::string bytes = text.SerializeAsString();

	// Tag of field 200, length delimited: (200 << 3) | 2 = 1602, as varint 0xC2 0x0C.
	CHECK(bytes.find(std::string("\xC2\x0C", 2)) != std::string::npos);
}
