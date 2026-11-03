// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>
#include <vector>

namespace mmo
{
	/// Number of attribute slots. Attributes themselves are game data (attributes.proto); an
	/// attribute's id is its slot and must be smaller than this. Every unit replicates one value,
	/// bonus and malus field per slot, so raising this changes the object field layout and requires
	/// a game::ProtocolVersion bump.
	constexpr uint32 MaxAttributes = 10;

	/// Number of 32 bit player fields that hold the attribute point costs, one byte per attribute.
	constexpr uint32 AttributePointCostFieldCount = (MaxAttributes + 3) / 4;

	/// @brief Computes a hash identifying the set of defined attributes.
	///
	/// Attribute point spending is only meaningful for the attribute set it was made under. The
	/// hash covers the attribute ids only, so renaming an attribute or changing its texts keeps
	/// existing spending, while adding or removing an attribute changes the hash and resets it.
	/// Works with both the server (proto::Attributes) and the client (proto_client::Attributes) schema.
	/// @param attributes Attribute list message with repeated `entry` that has an `id()`.
	/// @returns A non-zero hash (0 is reserved for "no spending recorded").
	template<typename TAttributes>
	uint32 ComputeAttributeLayoutHash(const TAttributes& attributes)
	{
		std::vector<uint32> ids;
		ids.reserve(attributes.entry_size());
		for (const auto& entry : attributes.entry())
		{
			ids.push_back(entry.id());
		}
		std::sort(ids.begin(), ids.end());

		// FNV-1a over the little endian bytes of every id
		uint32 hash = 2166136261u;
		for (const uint32 id : ids)
		{
			for (uint32 shift = 0; shift < 32; shift += 8)
			{
				hash ^= (id >> shift) & 0xFF;
				hash *= 16777619u;
			}
		}

		return hash != 0 ? hash : 1;
	}
}
