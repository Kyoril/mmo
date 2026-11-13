// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "owned_content.h"
#include "section_path.h"
#include "math/path_spline.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <vector>

namespace mmo::pcg
{
	/// Everything persisted about one section path control.
	struct SectionPathRecord
	{
		/// Stable id of the control.
		uint64 id = 0;
		/// Display name.
		String name;
		/// Asset path of the section the path repeats.
		String sectionAsset;
		/// Per-path settings.
		SectionPathParams params;
		/// Catmull-Rom, 3D, open or closed (a loop). Only the closed flag and the points are stored.
		PathSpline spline;
		/// Stable id per control point; must be parallel to spline.points.
		std::vector<uint64> pointIds;
		/// Next id handed out to a new control point. Reading raises it to max(pointIds) + 1 if it is lower.
		uint64 nextPointId = 1;
		/// What the last build put into the world (influence, terrain contribution, owned objects).
		OwnedContent content;
		/// Influence rect per section copy of the last build, in layout order (empty = never built, or
		/// read from a version 1 file). Used by the overlap check instead of the single influence box.
		std::vector<WorldRect> builtRects;
	};

	/// Writes the record as binary (magic PCGP, version 5; version 2 appends builtRects, version 3 the closed flag,
	/// floorInside and groundSmoothing, version 4 the mass layer and facade variety settings, version 5 the height mode keepGround). Deterministic: the same
	/// record produces the same bytes.
	/// @return False if pointIds is not parallel to spline.points.
	bool WriteSectionPathRecord(io::Writer& writer, const SectionPathRecord& record);

	/// Reads a record written by WriteSectionPathRecord (versions 1 to 5; version 1 leaves builtRects empty, versions 1
	/// and 2 read as open paths with the default floorInside and groundSmoothing and refuse height mode SmoothedGround,
	/// versions 1 to 4 refuse height mode KeepGround,
	/// versions 1 to 3 read with the mass and the facade variety off). Non-finite mass or facade values, flags other
	/// than 0/1 and a massPaintLayer outside -1..3 are refused.
	/// @return False on a bad magic, unsupported version or truncated/corrupt data; out is then unspecified.
	bool ReadSectionPathRecord(io::Reader& reader, SectionPathRecord& out);

	/// Which kind of record a .hpcg file holds.
	enum class RecordKind : uint8
	{
		/// Not a known record (or too short).
		Unknown,
		/// An oasis record (magic PCGO).
		Oasis,
		/// A section path record (magic PCGP).
		SectionPath,
		/// A graph instance record (magic PCGG).
		GraphInstance
	};

	/// Peeks the magic: which record type the stream holds. The reader position is restored.
	RecordKind PeekRecordKind(io::Reader& reader);
}
