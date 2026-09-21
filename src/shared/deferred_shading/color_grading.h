// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	/// @brief Parametric colour grading applied after tone mapping (see docs/color-grading.md).
	/// @remark Dependency-free apart from base and math so the headless deferred_shading_tests target can compile it.
	struct ColorGradingSettings
	{
		/// @brief 0 = grey, 1 = unchanged.
		float saturation = 1.0f;

		/// @brief Contrast around mid-grey, 1 = unchanged.
		float contrast = 1.0f;

		/// @brief Multiplies the graded colour, 1 = unchanged.
		Vector3 colorFilter { 1.0f, 1.0f, 1.0f };

		/// @brief Sets the saturation, clamped to [0, 2].
		void SetSaturation(const float value)
		{
			saturation = std::clamp(value, 0.0f, 2.0f);
		}

		/// @brief Sets the contrast, clamped to [0, 2].
		void SetContrast(const float value)
		{
			contrast = std::clamp(value, 0.0f, 2.0f);
		}

		/// @brief Sets the colour filter, each channel clamped to [0, 2].
		void SetColorFilter(const Vector3& value)
		{
			colorFilter = Vector3(std::clamp(value.x, 0.0f, 2.0f), std::clamp(value.y, 0.0f, 2.0f), std::clamp(value.z, 0.0f, 2.0f));
		}
	};

	/// @brief Strip LUT addressing shared with PS_Tonemap.hlsl - change both together.
	namespace color_grading
	{
		/// @brief Largest saturation, contrast or colour filter channel.
		constexpr float MaxGradingValue = 2.0f;

		/// @brief Whether a texture is a strip LUT: N*N wide, N high, N >= 2.
		[[nodiscard]] inline bool IsStripLut(const uint32 width, const uint32 height)
		{
			return height >= 2 && width == height * height;
		}

		/// @brief Texture coordinates of the two reads that look up one colour.
		struct StripCoordinates
		{
			/// @brief u of the read in the lower slice.
			float u0;

			/// @brief u of the read in the upper slice.
			float u1;

			/// @brief v of both reads.
			float v;

			/// @brief Weight of the upper slice.
			float sliceBlend;
		};

		/// @brief Where to read colour (r, g, b) in a strip LUT of edge size. Input is clamped to [0, 1].
		[[nodiscard]] inline StripCoordinates StripLutCoordinates(float r, float g, float b, const float size)
		{
			r = std::clamp(r, 0.0f, 1.0f);
			g = std::clamp(g, 0.0f, 1.0f);
			b = std::clamp(b, 0.0f, 1.0f);

			const float scale = size - 1.0f;
			const float slice = b * scale;
			const float slice0 = std::floor(slice);
			const float slice1 = std::min(slice0 + 1.0f, scale);
			const float column = r * scale + 0.5f;
			const float width = size * size;

			StripCoordinates coordinates;
			coordinates.u0 = (slice0 * size + column) / width;
			coordinates.u1 = (slice1 * size + column) / width;
			coordinates.v = (g * scale + 0.5f) / size;
			coordinates.sliceBlend = slice - slice0;
			return coordinates;
		}
	}
}
