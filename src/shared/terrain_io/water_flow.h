// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <vector>

namespace mmo
{
	namespace terrain_io
	{
		/// @brief River flow authoring: turns river paths into the per-vertex flow UVs and speeds of a
		///	       page's WFLW chunk. Shared by terrain_tool's delta import and the editor's flow brush.
		namespace water_flow
		{
			/// @brief One point of a river's flow path: position and half width of the channel there.
			struct FlowPoint
			{
				float x = 0.0f;
				float z = 0.0f;
				float halfWidth = 10.0f;
			};

			/// @brief A river path in downstream order.
			typedef std::vector<FlowPoint> FlowPath;

			/// @brief Parameters shared by all paths of a world.
			struct FlowSettings
			{
				/// @brief Water UV units per world unit along and across the flow (the world mapping uses 1/16).
				float uvScale = 1.0f / 16.0f;

				/// @brief Distance beyond the channel's half width over which the flow slows to minSpeed.
				float falloff = 60.0f;

				/// @brief Slowest flow (0..1), used for still water off the channel such as bays and ponds.
				float minSpeed = 0.08f;
			};

			/// @brief Flow coordinate of a point relative to a river path.
			struct FlowCoordinate
			{
				/// @brief Distance downstream along the path.
				float along = 0.0f;

				/// @brief Signed distance across the path, positive to the left of the flow seen from
				///	       above, so the mapping is a rotation of the world mapping.
				float across = 0.0f;

				/// @brief Channel half width at the projected point.
				float halfWidth = 0.0f;
			};

			/// @brief Projects a point onto the nearest segment of a path.
			/// @param path The path. Needs at least two points to give a meaningful result.
			/// @param x World x of the point.
			/// @param z World z of the point.
			/// @return The flow coordinate of the point.
			FlowCoordinate ProjectOntoFlowPath(const FlowPath& path, float x, float z);

			/// @brief Whether any usable path (two points or more) exists.
			bool HasUsablePath(const std::vector<FlowPath>& paths);

			/// @brief Whether a world-space rectangle lies within a path's channel plus falloff, i.e.
			///	       whether the flow there is faster than the minimum speed.
			/// @param paths The river paths.
			/// @param settings The flow settings (falloff is used).
			/// @param minX World x of the rectangle's minimum corner.
			/// @param minZ World z of the rectangle's minimum corner.
			/// @param maxX World x of the rectangle's maximum corner.
			/// @param maxZ World z of the rectangle's maximum corner.
			bool IsRectInReach(const std::vector<FlowPath>& paths, const FlowSettings& settings, float minX, float minZ, float maxX, float maxZ);

			/// @brief Computes one page's flow data.
			/// @param paths The river paths. Each vertex takes its flow from the nearest path.
			/// @param settings The flow settings.
			/// @param pageOriginX World x of the page's first outer vertex.
			/// @param pageOriginZ World z of the page's first outer vertex.
			/// @param outUVs Receives OuterVerticesPerPageSide² (u, v) pairs, u downstream and v across.
			/// @param outSpeeds Receives OuterVerticesPerPageSide² speeds (0..255).
			/// @remark The flow coordinates are smoothed: the nearest path segment switches on the inside
			///	        of bends, which makes "along" jump by a little, and every jump shows as a seam in
			///	        the panned ripples. The page is computed with a border wide enough for the blur, so
			///	        neighbouring pages computed on their own still agree on their shared vertices.
			void ComputePageFlow(const std::vector<FlowPath>& paths, const FlowSettings& settings, float pageOriginX, float pageOriginZ,
				std::vector<float>& outUVs, std::vector<uint8>& outSpeeds);
		}
	}
}
