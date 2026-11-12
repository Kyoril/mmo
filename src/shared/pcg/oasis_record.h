// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "oasis.h"
#include "owned_content.h"
#include "math/path_spline.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <vector>

namespace mmo::pcg
{
	/// Everything persisted about one oasis instance.
	struct OasisRecord
	{
		uint64 id = 0;
		String name;
		uint64 seed = 1;
		OasisParams params;
		/// Closed, Catmull-Rom.
		PathSpline loop;
		/// False when the record was read from a file older than version 3, which stored no point
		/// heights: the loop's y and tangent heights are zero and the editor fills them from the
		/// terrain. Not serialized (writing always stores heights).
		bool heightsValid = true;
		/// What the last build put into the world (influence, terrain contribution, owned objects).
		OwnedContent content;
	};

	/// Writes the record as chunked binary (magic PCGO, version 4). Deterministic: the same record
	/// produces the same bytes.
	/// @return True on success.
	bool WriteOasisRecord(io::Writer& writer, const OasisRecord& record);

	/// Reads a record written by WriteOasisRecord.
	/// Accepts versions 1 to 4; version 1 loop points get automatic tangents, and records older than
	/// version 3 have heightsValid = false.
	/// @return False on a bad magic, unsupported version or truncated/corrupt data; out is then unspecified.
	bool ReadOasisRecord(io::Reader& reader, OasisRecord& out);
}
