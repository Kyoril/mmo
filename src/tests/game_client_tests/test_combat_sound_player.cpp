// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "fake_audio.h"

#include "game_client/combat_sound_player.h"
#include "game_client/sound_entry_player.h"

using namespace mmo;

namespace
{
	constexpr uint32 kSubclassSword = 1;
	constexpr uint32 kModelHuman = 100;

	constexpr uint32 kSoundSwing = 500;
	constexpr uint32 kSoundImpact = 501;
	constexpr uint32 kSoundCritLayer = 502;
	constexpr uint32 kSoundAttackVoice = 700;
	constexpr uint32 kSoundHitVoice = 702;

	/// Project slice plus doubles, wired the way the client wires them.
	struct PlayerFixture
	{
		proto_client::Project project;
		FakeAudio audio;
		SoundEntryPlayer soundPlayer{ audio, project.sounds };

		PlayerFixture()
		{
			AddSound(kSoundSwing, "swing.wav");
			AddSound(kSoundImpact, "impact.wav");
			AddSound(kSoundCritLayer, "crit.wav");
			AddSound(kSoundAttackVoice, "attack_voice.wav");
			AddSound(kSoundHitVoice, "hit_voice.wav");

			auto* sword = project.itemSubclasses.add(kSubclassSword);
			sword->set_name("Sword");
			sword->set_itemclass(0);
			sword->set_swing_sound(kSoundSwing);
			sword->set_crit_layer_sound(kSoundCritLayer);
			auto* impact = sword->add_impact_sounds();
			impact->set_target_material(0);
			impact->set_sound(kSoundImpact);

			auto* model = project.models.add(kModelHuman);
			model->set_name("Human");
			model->set_filename("human.hmsh");
			auto* sounds = model->mutable_combat_sounds();
			sounds->set_attack_voice_sound(kSoundAttackVoice);
			sounds->set_attack_voice_chance(50);
			sounds->set_hit_voice_sound(kSoundHitVoice);
			sounds->set_hit_voice_chance(50);
			sounds->set_voice_min_interval_ms(1000);
		}

		void AddSound(const uint32 id, const String& file)
		{
			auto* entry = project.sounds.add(id);
			entry->set_name(file);
			entry->add_files(file);
		}

		bool Played(const String& file) const
		{
			return audio.PlayedFile(file);
		}
	};

	CombatSoundAttacker MakeAttacker()
	{
		CombatSoundAttacker attacker;
		attacker.weaponSubclass = kSubclassSword;
		attacker.displayId = kModelHuman;
		return attacker;
	}

	CombatSoundVictim MakeVictim()
	{
		CombatSoundVictim victim;
		victim.displayId = kModelHuman;
		return victim;
	}
}

TEST_CASE("A swing plays the weapon whoosh", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 99u; });   // never passes a 50% chance
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);

	CHECK(fixture.Played("swing.wav"));
	CHECK_FALSE(fixture.Played("attack_voice.wav"));
}

TEST_CASE("A landed hit plays impact, and a crit layers the crit sound", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 99u; });
	player.SetClock([] { return GameTime(0); });

	CombatSwingOutcome outcome;
	outcome.landed = true;
	outcome.crit = true;

	player.PlayOutcome(MakeAttacker(), MakeVictim(), outcome, 2, Vector3::Zero);

	CHECK(fixture.Played("impact.wav"));
	CHECK(fixture.Played("crit.wav"));
}

TEST_CASE("A voice line plays when the roll is under the chance", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 10u; });   // under the authored 50%
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);

	CHECK(fixture.Played("attack_voice.wav"));
}

TEST_CASE("A voice line is refused when the roll is at or over the chance", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 50u; });   // exactly at the authored 50%
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);

	CHECK_FALSE(fixture.Played("attack_voice.wav"));
}

TEST_CASE("A chance of zero never plays, whatever the roll", "[combat_sound_player]")
{
	PlayerFixture fixture;
	auto* sounds = fixture.project.models.getById(kModelHuman)->mutable_combat_sounds();
	sounds->set_attack_voice_chance(0);

	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 0u; });   // the lowest possible roll
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);

	CHECK_FALSE(fixture.Played("attack_voice.wav"));
	CHECK(fixture.Played("swing.wav"));
}

TEST_CASE("A chance of one hundred always plays, whatever the roll", "[combat_sound_player]")
{
	PlayerFixture fixture;
	auto* sounds = fixture.project.models.getById(kModelHuman)->mutable_combat_sounds();
	sounds->set_attack_voice_chance(100);

	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 99u; });   // the highest possible roll
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);

	CHECK(fixture.Played("attack_voice.wav"));
}

TEST_CASE("A unit does not talk over itself inside the throttle window", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 0u; });

	GameTime now = 0;
	player.SetClock([&now] { return now; });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);
	REQUIRE(fixture.Played("attack_voice.wav"));

	fixture.audio.playedFiles.clear();

	now = 500;   // inside the authored 1000ms interval
	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);
	CHECK_FALSE(fixture.Played("attack_voice.wav"));

	now = 1000;
	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);
	CHECK(fixture.Played("attack_voice.wav"));
}

TEST_CASE("The attacker and the victim are gated independently", "[combat_sound_player]")
{
	PlayerFixture fixture;
	CombatSoundPlayer player(fixture.project, fixture.soundPlayer);
	player.SetRollProvider([] { return 0u; });
	player.SetClock([] { return GameTime(0); });

	player.PlaySwing(MakeAttacker(), 1, Vector3::Zero, false);
	REQUIRE(fixture.Played("attack_voice.wav"));

	fixture.audio.playedFiles.clear();

	CombatSwingOutcome outcome;
	outcome.landed = true;

	// Victim guid 2 has its own gate, so its pain react is not blocked by the attacker's line.
	player.PlayOutcome(MakeAttacker(), MakeVictim(), outcome, 2, Vector3::Zero);
	CHECK(fixture.Played("hit_voice.wav"));
}
