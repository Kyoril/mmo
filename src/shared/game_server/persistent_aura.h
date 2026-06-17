// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <vector>

#include "base/typedefs.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	/// A single persisted aura effect: the spell effect index it originated from and the
	/// cast-time base points that were applied. Stored so a restored aura keeps its exact
	/// magnitudes without needing the original caster to be online.
	struct PersistentAuraEffect
	{
		uint32 effectIndex = 0;
		int32 basePoints = 0;
	};

	/// Snapshot of a single active aura that should survive logout / teleport.
	/// Only non-passive auras that are not granted by equipment are persisted.
	struct PersistentAuraData
	{
		uint32 spellId = 0;
		uint64 casterId = 0;
		/// Remaining duration in milliseconds; 0 means the aura does not expire.
		GameTime remainingDuration = 0;
		/// Current stack count (always >= 1).
		uint32 stackCount = 1;
		/// Per-effect base point snapshot.
		std::vector<PersistentAuraEffect> effects;
		/// Reserved: when true the aura expires by wall-clock time instead of remaining
		/// duration. Not yet acted upon by the runtime (future work).
		bool realtime = false;
	};

	/// Snapshot of a single spell cooldown. Transferred between world nodes as a remaining
	/// duration; the realm server converts this to/from a wall-clock end timestamp for the DB.
	struct PersistentCooldownData
	{
		uint32 spellId = 0;
		/// Remaining cooldown in milliseconds.
		GameTime remainingMs = 0;
	};

	inline io::Writer& operator<<(io::Writer& writer, const PersistentAuraData& data)
	{
		writer
			<< io::write<uint32>(data.spellId)
			<< io::write<uint64>(data.casterId)
			<< io::write<uint64>(data.remainingDuration)
			<< io::write<uint32>(data.stackCount)
			<< io::write<uint8>(data.realtime ? 1 : 0)
			<< io::write<uint8>(static_cast<uint8>(data.effects.size()));
		for (const auto& effect : data.effects)
		{
			writer
				<< io::write<uint32>(effect.effectIndex)
				<< io::write<int32>(effect.basePoints);
		}
		return writer;
	}

	inline io::Reader& operator>>(io::Reader& reader, PersistentAuraData& data)
	{
		uint8 realtime = 0;
		uint8 effectCount = 0;
		if (!(reader
			>> io::read<uint32>(data.spellId)
			>> io::read<uint64>(data.casterId)
			>> io::read<uint64>(data.remainingDuration)
			>> io::read<uint32>(data.stackCount)
			>> io::read<uint8>(realtime)
			>> io::read<uint8>(effectCount)))
		{
			return reader;
		}

		data.realtime = (realtime != 0);
		data.effects.clear();
		data.effects.reserve(effectCount);
		for (uint8 i = 0; i < effectCount; ++i)
		{
			PersistentAuraEffect effect;
			if (!(reader >> io::read<uint32>(effect.effectIndex) >> io::read<int32>(effect.basePoints)))
			{
				return reader;
			}
			data.effects.push_back(effect);
		}
		return reader;
	}

	inline io::Writer& operator<<(io::Writer& writer, const PersistentCooldownData& data)
	{
		writer
			<< io::write<uint32>(data.spellId)
			<< io::write<uint64>(data.remainingMs);
		return writer;
	}

	inline io::Reader& operator>>(io::Reader& reader, PersistentCooldownData& data)
	{
		reader
			>> io::read<uint32>(data.spellId)
			>> io::read<uint64>(data.remainingMs);
		return reader;
	}
}
