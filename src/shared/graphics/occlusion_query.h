// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	/// @brief Abstract interface for GPU occlusion queries.
	///
	/// Occlusion queries allow the GPU to report how many pixels passed the depth
	/// test for geometry rendered between Begin() and End() calls. This information
	/// can be used to cull objects that are completely hidden behind other geometry,
	/// reducing unnecessary draw calls and improving rendering performance.
	///
	/// Typical usage pattern (previous-frame approach):
	/// 1. Frame N: Render object, wrap draw calls with Begin()/End()
	/// 2. Frame N+1: Call TryGetResult() — if 0 pixels, skip rendering
	/// 3. Periodically re-test skipped objects to detect visibility changes
	class OcclusionQuery : public NonCopyable
	{
	public:
		/// @brief Virtual destructor.
		virtual ~OcclusionQuery() = default;

		/// @brief Begins the occlusion query. All draw calls after this and
		///        before End() will be tested for visibility.
		virtual void Begin() = 0;

		/// @brief Ends the occlusion query. Results become available asynchronously.
		virtual void End() = 0;

		/// @brief Attempts to retrieve the query result without blocking.
		/// @param pixelCount Receives the number of pixels that passed the depth test.
		/// @return true if the result is available, false if the query is still pending.
		virtual bool TryGetResult(uint64& pixelCount) = 0;

		/// @brief Returns true if a query has been issued and results may be available.
		virtual bool HasPendingResult() const = 0;
	};

	typedef std::unique_ptr<OcclusionQuery> OcclusionQueryPtr;
}
