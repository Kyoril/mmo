// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "owned_content.h"
#include "math/path_spline.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo::pcg::record_io
{
	/// Writes a plain value.
	template<typename T>
	void Put(io::Writer& w, const T& value)
	{
		w.WritePOD(value);
	}

	/// Reads a plain value.
	/// @return False if the stream failed.
	template<typename T>
	bool Get(io::Reader& r, T& value)
	{
		r.readPOD(value);
		return static_cast<bool>(r);
	}

	/// Writes a length-prefixed string.
	void PutString(io::Writer& w, const String& s);

	/// Reads a length-prefixed string (bounded by the remaining data).
	bool GetString(io::Reader& r, String& s);

	/// Bytes left in the reader's source.
	size_t Remaining(io::Reader& r);

	/// Reads an element count and rejects counts that cannot fit in the remaining data.
	bool GetCount(io::Reader& r, uint32& count, size_t minElementBytes);

	/// Writes a rectangle (min x, min z, max x, max z).
	void PutRect(io::Writer& w, const WorldRect& r);

	/// Reads a rectangle written by PutRect.
	bool GetRect(io::Reader& r, WorldRect& v);

	/// Bytes per spline point written by PutSplinePoint.
	constexpr size_t SplinePointBytes = 41;

	/// Writes one spline point with tangents and heights (41 bytes).
	void PutSplinePoint(io::Writer& w, const SplinePoint& pt);

	/// Reads one spline point written by PutSplinePoint.
	bool GetSplinePoint(io::Reader& r, SplinePoint& pt);

	/// Writes owned content: influence rect, terrain contribution, owned placements.
	void PutOwnedContent(io::Writer& w, const OwnedContent& content);

	/// Reads owned content. @param version Placement layout version (4 = current, with overrides and kits).
	bool GetOwnedContent(io::Reader& r, OwnedContent& content, uint32 version);
}
