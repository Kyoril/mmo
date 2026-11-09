// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace upscale_filter
	{
		/// @brief How a frame rendered below output resolution is brought up to it. The values are
		///			stored in the gxUpscaleFilter console variable, so they must not change.
		enum Type
		{
			/// Nearest texel: crisp, blocky.
			Point = 0,

			/// Hardware bilinear: soft. Costs no pass at all.
			Bilinear = 1,

			/// Catmull-Rom bicubic: sharper than bilinear, a little softer than FSR.
			Bicubic = 2,

			/// AMD FidelityFX Super Resolution 1.0 (EASU upscale followed by RCAS sharpening).
			Fsr = 3,

			Count_
		};
	}

	/// @brief Clamps a raw console variable value to a valid upscale filter.
	/// @param value The raw value. Out-of-range values fall back to FSR, the default.
	[[nodiscard]] upscale_filter::Type ClampUpscaleFilter(int32 value);

	/// @brief Converts the RCAS sharpening strength in stops (0 = strongest, each stop halves it) to
	///			the factor the shader multiplies its lobe with.
	/// @param stops Strength in stops, clamped to [0, 2].
	[[nodiscard]] float RcasSharpnessFromStops(float stops);

	/// @brief Whether bringing a source of the given size to the given output size needs a pass.
	/// @return False if source and output have the same size, or if the filter is bilinear, which
	///			the final textured quad already does for free.
	[[nodiscard]] bool UpscaleNeedsPass(upscale_filter::Type filter, uint32 sourceWidth, uint32 sourceHeight,
		uint32 outputWidth, uint32 outputHeight);
}
