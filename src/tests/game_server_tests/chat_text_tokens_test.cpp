// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Npc chat lines raised by a trigger are authored with the "$N" token, the same token quest and
// gossip text use for the player's name (see the soul binder's "Tomas Reedley - Bind Player"
// trigger). The token has to be replaced before the line goes on the wire, in every locale, or
// the npc says "$N" out loud.

#include "catch.hpp"
#include "persistent_pet_fixture.h"

#include "game_server/chat_text_tokens.h"
#include "game_server/world/tile_subscriber.h"
#include "game_server/world/visibility_grid.h"
#include "game_server/world/visibility_tile.h"
#include "game/chat_type.h"
#include "shared/proto_data/localization.pb.h"

#include <string>
#include <vector>

using namespace mmo;
using namespace mmo::pet_tests;

namespace
{
	/// A client listening to what the units around it say, in its own locale.
	struct ChatListener final : TileSubscriber
	{
		ChatListener(GameUnitS& unit, const LocaleIndex locale)
			: unit(unit)
			, locale(locale)
		{
		}

		GameUnitS& GetGameUnit() const override { return unit; }

		LocaleIndex GetLocale() const override { return locale; }

		void NotifyObjectsUpdated(const std::vector<GameObjectS*>&) override {}
		void NotifyObjectsSpawned(const std::vector<GameObjectS*>&, game::spawn_reason::Type) override {}
		void NotifyObjectsDespawned(const std::vector<GameObjectS*>&, game::despawn_reason::Type) override {}
		GameTime ClientToServerTime(const GameTime clientTimestamp) const override { return clientTimestamp; }
		GameTime ServerToClientTime(const GameTime serverTimestamp) const override { return serverTimestamp; }
		bool HasReceivedTimeSyncResponse() const override { return true; }

		void SendPacket(game::Protocol::OutgoingPacket& packet, const std::vector<char>& buffer, bool) override
		{
			if (packet.GetId() != game::realm_client_packet::ChatMessage)
			{
				return;
			}

			// Skip the packet header (opcode + size) the outgoing packet wrote in front of the body.
			const size_t headerSize = buffer.size() - packet.GetSize();
			io::MemorySource source(buffer.data() + headerSize, buffer.data() + buffer.size());
			io::Reader reader(source);

			uint64 guid = 0;
			uint8 type = 0;
			String message;
			reader >> io::read_packed_guid(guid) >> io::read<uint8>(type) >> io::read_limited_string<512>(message);
			if (reader)
			{
				lines.push_back(message);
			}
		}

		GameUnitS& unit;
		LocaleIndex locale;
		std::vector<String> lines;
	};
}

TEST_CASE("SubstituteChatTextTokens replaces the player name token", "[chat_text_tokens]")
{
	REQUIRE(SubstituteChatTextTokens("Done. Walk whichever road you like, $N - this stone knows your name now.", "Kyo")
		== "Done. Walk whichever road you like, Kyo - this stone knows your name now.");
	REQUIRE(SubstituteChatTextTokens("$n, and $N again.", "Kyo") == "Kyo, and Kyo again.");
}

TEST_CASE("SubstituteChatTextTokens leaves lines without an addressee and unknown tokens alone", "[chat_text_tokens]")
{
	// No addressee: nothing to put in, so nothing is invented.
	REQUIRE(SubstituteChatTextTokens("Greetings, $N.", "") == "Greetings, $N.");

	// Only the name token is ours; everything else stays as authored.
	REQUIRE(SubstituteChatTextTokens("You owe me $5, $C.", "Kyo") == "You owe me $5, $C.");
	REQUIRE(SubstituteChatTextTokens("Trailing $", "Kyo") == "Trailing $");
}

TEST_CASE_METHOD(PersistentPetFixture, "An npc chat line addressed to a player says the player's name in every locale", "[chat_text_tokens]")
{
	auto speaker = SpawnCreature(*boarUnit, Vector3(0.0f, 0.0f, 0.0f));

	ChatListener english(*speaker, LocaleIndex::enUS);
	ChatListener german(*speaker, LocaleIndex::deDE);

	TileIndex2D tileIndex;
	world->GetGrid().GetTilePosition(speaker->GetPosition(), tileIndex[0], tileIndex[1]);
	VisibilityTile& tile = world->GetGrid().RequireTile(tileIndex);
	tile.GetWatchers().add(&english);
	tile.GetWatchers().add(&german);

	google::protobuf::RepeatedPtrField<proto::LocalizedString> localized;
	auto* germanText = localized.Add();
	germanText->set_locale(static_cast<uint32>(LocaleIndex::deDE));
	germanText->set_value("Geschehen, $N.");

	speaker->ChatSay("Done, $N.", &localized, "Kyo");

	tile.GetWatchers().remove(&english);
	tile.GetWatchers().remove(&german);

	REQUIRE(english.lines.size() == 1);
	REQUIRE(english.lines[0] == "Done, Kyo.");
	REQUIRE(german.lines.size() == 1);
	REQUIRE(german.lines[0] == "Geschehen, Kyo.");
}
