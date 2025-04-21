// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/quest.h"

#include <array>

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	struct QuestStatusData
	{
		QuestStatus status;

		// May be 0 if completed.
		GameTime expiration;

		// What is this for?
		bool explored;

		std::array<uint16, 4> creatures;

		QuestStatusData()
			: status(QuestStatus::Available)
			, expiration(0)
			, explored(false)
		{
			creatures.fill(0);
		}
	};

	io::Writer& operator<<(io::Writer& w, const QuestStatusData& object);
	io::Reader& operator>>(io::Reader& r, QuestStatusData& object);

}