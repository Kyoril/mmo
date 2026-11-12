// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/path_spline.h"

#include <utility>
#include <vector>

namespace mmo::pcg::graph
{
	/// @brief Result of a PolylineIndex::Nearest query.
	struct NearestHit
	{
		/// @brief Distance (m, x/z) from the query to the nearest point of the polyline.
		float distance = 0.0f;

		/// @brief Sample indices of the nearest segment's ends (a == b for a single-sample polyline).
		uint32 a = 0;
		uint32 b = 0;

		/// @brief Parameter (0..1) of the nearest point between sample a and sample b.
		float t = 0.0f;
	};

	/// @brief Nearest segment of an x/z polyline within a fixed reach.
	/// @details The segments are bucketed into a uniform grid: every cell lists (ascending) the segments whose bounds
	///          grown by the reach touch it, so the cell holding a point lists every segment within reach of it. The
	///          query is exact: the minimum distance over the listed segments, ties to the lowest segment index.
	///          Segments are (i, i + 1) in sample order, plus (last, 0) for a closed polyline of at least 3 samples; a
	///          single sample is one degenerate segment (0, 0).
	class PolylineIndex final
	{
	public:
		/// @brief Builds the index (replacing what was built before).
		/// @param samples The polyline (y is kept but ignored).
		/// @param closed True joins the last sample to the first (with at least 3 samples).
		/// @param reach Largest distance a query finds a segment at; negative counts as 0.
		/// @param skipSegments If not empty: entry s non-zero leaves segment s out (segments numbered as above: (i, i + 1) for
		///        i = 0.., then (last, 0) if closed); a shorter vector keeps the rest.
		void Build(const std::vector<SplineSample>& samples, bool closed, float reach, const std::vector<uint8>& skipSegments = {});

		/// @brief Nearest segment within the reach of (x, z).
		/// @param hit Receives the segment and distance when the call returns true.
		/// @return False if no segment lies within the reach (or nothing was built).
		bool Nearest(float x, float z, NearestHit& hit) const;

		/// @brief The polyline's samples as built.
		const std::vector<SplineSample>& GetSamples() const
		{
			return m_samples;
		}

		/// @brief Whether the polyline was built closed.
		bool IsClosed() const
		{
			return m_closed;
		}

	private:
		/// @brief Grid cell index of a coordinate, clamped to [0, count - 1].
		int64 CellOf(double v, double origin, int64 count) const;

	private:
		std::vector<SplineSample> m_samples;
		bool m_closed = false;
		std::vector<std::pair<uint32, uint32>> m_segments;
		std::vector<uint32> m_cellStart;
		std::vector<uint32> m_cells;
		float m_reach = 0.0f;
		double m_cellSize = 1.0;
		double m_originX = 0.0;
		double m_originZ = 0.0;
		int64 m_columns = 0;
		int64 m_rows = 0;
	};
}
