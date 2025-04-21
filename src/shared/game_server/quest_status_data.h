// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/quest.h"

#include <array>

// Forward declarations
namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	/// Structure that contains the status of a single quest.
	struct QuestStatusData
	{
		/// The quest status enum.
		QuestStatus status;

		/// The timestamp at which this quest will expire.
		GameTime expiration;

		/// Set to true if the quest exploration flag was set.
		bool explored;

		/// Contains the number of creatures killed for this quest.
		std::array<uint16, 4> creatures;

		/// Default constructor which initializes the quest status to the default values which marks
		/// an uncompleted, not started quest.
		QuestStatusData()
			: status(QuestStatus::Available)
			, expiration(0)
			, explored(false)
		{
			creatures.fill(0);
		}
	};

	/// Serialization function for the QuestStatusData structure to a binary stream.
	/// @param w The writer to serialize the data to.
	/// @param object The QuestStatusData object to serialize.
	/// @returns The writer after serialization.
	io::Writer& operator<<(io::Writer& w, const QuestStatusData& object);

	/// Deserialization function for the QuestStatusData structure from a binary stream.
	/// @param r The reader to deserialize the data from.
	/// @param object The QuestStatusData object to deserialize into.
	/// @returns The reader after deserialization.
	io::Reader& operator>>(io::Reader& r, QuestStatusData& object);

}