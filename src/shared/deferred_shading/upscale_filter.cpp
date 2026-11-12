// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "upscale_filter.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	upscale_filter::Type ClampUpscaleFilter(const int32 value)
	{
		if (value < 0 || value >= upscale_filter::Count_)
		{
			return upscale_filter::Fsr;
		}

		return static_cast<upscale_filter::Type>(value);
	}

	float RcasSharpnessFromStops(const float stops)
	{
		return std::exp2(-std::clamp(stops, 0.0f, 2.0f));
	}

	bool UpscaleNeedsPass(const upscale_filter::Type filter, const uint32 sourceWidth, const uint32 sourceHeight,
		const uint32 outputWidth, const uint32 outputHeight)
	{
		if (filter == upscale_filter::Bilinear)
		{
			return false;
		}

		if (sourceWidth == 0 || sourceHeight == 0 || outputWidth == 0 || outputHeight == 0)
		{
			return false;
		}

		return sourceWidth != outputWidth || sourceHeight != outputHeight;
	}
}
