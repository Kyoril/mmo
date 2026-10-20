// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game/action_button.h"

#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"

#include "catch.hpp"

#include <vector>

using namespace mmo;

TEST_CASE("Characters own two full bars worth of action buttons", "[action_button]")
{
	// The UI shows a main bar and a second bar floating above it, twelve slots each. The second bar
	// is useless without its own slots, so the slot count has to cover both bars.
	CHECK(ActionButtonsPerBar == 12);
	CHECK(ActionBarCount == 2);
	REQUIRE(MaxActionButtons == 24);

	const ActionButtons buttons{};
	CHECK(buttons.size() == 24);
}

TEST_CASE("Second bar action buttons survive the action buttons payload", "[action_button]")
{
	REQUIRE(MaxActionButtons == 24);

	ActionButtons buttons{};
	buttons[0] = ActionButton{ 7, action_button_type::Spell };

	// Slots 12..23 are the second action bar.
	buttons[12] = ActionButton{ 1234, action_button_type::Spell };
	buttons[23] = ActionButton{ 4321, action_button_type::Item };

	std::vector<char> buffer;
	io::VectorSink sink{ buffer };
	io::Writer writer{ sink };
	writer << io::write_range(buttons);

	io::MemorySource source{ buffer };
	io::Reader reader{ source };

	ActionButtons readBack{};
	reader >> io::read_range(readBack);

	REQUIRE(static_cast<bool>(reader));
	CHECK(readBack[0].action == 7);
	CHECK(readBack[12].action == 1234);
	CHECK(readBack[12].type == action_button_type::Spell);
	CHECK(readBack[23].action == 4321);
	CHECK(readBack[23].type == action_button_type::Item);
}
