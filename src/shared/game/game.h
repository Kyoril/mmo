// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "stduuid/uuid.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"

namespace mmo
{
	typedef uint32 MapId;

	typedef uuids::uuid InstanceId;

	typedef uint64 ObjectId;

	inline io::Writer& operator<<(io::Writer& writer, const InstanceId& instanceId)
	{
		const auto bytes = instanceId.as_bytes();
		return writer
			<< io::write_range(bytes.begin(), bytes.end());
	}
	
	inline io::Reader& operator>>(io::Reader& reader, InstanceId& instanceId)
	{
		std::array<uint8, 16> data{};
		if (reader >> io::read_range(data))
		{
			instanceId = data;
		}

		return reader;
	}
}
