// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace terrain
	{
		/// @brief A brush footprint swept from one world position to another.
		///
		/// A brush applied once per frame at a single point lays down separate blobs as soon as
		/// the cursor moves further than its own diameter between frames — which a far camera
		/// makes trivial, since a few pixels of pointer movement then span a large distance in
		/// world space. Sweeping the footprint along the segment the cursor actually covered
		/// gives gap-free coverage at any speed, and costs the swept area rather than the area
		/// of every sample along it.
		///
		/// A stationary brush is the degenerate zero-length case, for which the swept distance
		/// is identical to the plain radial distance.
		struct BrushStroke
		{
			float fromX = 0.0f;
			float fromZ = 0.0f;
			float toX = 0.0f;
			float toZ = 0.0f;

			/// @brief Builds a stationary (zero-length) stroke at a single world position.
			/// @param x World X position.
			/// @param z World Z position.
			/// @return A stroke whose swept distance equals the radial distance from (x, z).
			static BrushStroke At(const float x, const float z) { return BrushStroke{ x, z, x, z }; }

			/// @brief Shortest distance in the XZ plane from a world position to this stroke.
			/// @param x World X position to measure from.
			/// @param z World Z position to measure from.
			/// @return Distance to the nearest point on the swept segment.
			[[nodiscard]] float DistanceTo(const float x, const float z) const
			{
				const float segX = toX - fromX;
				const float segZ = toZ - fromZ;
				const float lengthSq = segX * segX + segZ * segZ;

				float offsetX = x - fromX;
				float offsetZ = z - fromZ;

				if (lengthSq > 0.0f)
				{
					// Project onto the segment and clamp, so the ends stay round rather than
					// extending the footprint past them.
					const float t = std::min(1.0f, std::max(0.0f, (offsetX * segX + offsetZ * segZ) / lengthSq));
					offsetX -= segX * t;
					offsetZ -= segZ * t;
				}

				return std::sqrt(offsetX * offsetX + offsetZ * offsetZ);
			}

			[[nodiscard]] float MinX() const { return std::min(fromX, toX); }
			[[nodiscard]] float MaxX() const { return std::max(fromX, toX); }
			[[nodiscard]] float MinZ() const { return std::min(fromZ, toZ); }
			[[nodiscard]] float MaxZ() const { return std::max(fromZ, toZ); }
		};
	}
}
