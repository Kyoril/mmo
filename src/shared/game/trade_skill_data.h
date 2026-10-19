// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	/// A character's progress in one trade skill (profession).
	struct TradeSkillData
	{
		/// SkillEntry id of the trade skill.
		uint32 skillId = 0;
		/// Current skill value.
		uint16 value = 0;
		/// Current skill cap.
		uint16 max = 0;
	};

	inline io::Writer& operator<<(io::Writer& writer, const TradeSkillData& data)
	{
		return writer
			<< io::write<uint32>(data.skillId)
			<< io::write<uint16>(data.value)
			<< io::write<uint16>(data.max);
	}

	inline io::Reader& operator>>(io::Reader& reader, TradeSkillData& data)
	{
		return reader
			>> io::read<uint32>(data.skillId)
			>> io::read<uint16>(data.value)
			>> io::read<uint16>(data.max);
	}
}
