// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "deferred_shading/upscale_filter.h"

using namespace mmo;

TEST_CASE("ClampUpscaleFilter keeps the stored console values and falls back to FSR", "[upscale]")
{
	// The cvar stores these numbers, so they must keep meaning the same filter.
	CHECK(ClampUpscaleFilter(0) == upscale_filter::Point);
	CHECK(ClampUpscaleFilter(1) == upscale_filter::Bilinear);
	CHECK(ClampUpscaleFilter(2) == upscale_filter::Bicubic);
	CHECK(ClampUpscaleFilter(3) == upscale_filter::Fsr);

	CHECK(ClampUpscaleFilter(-1) == upscale_filter::Fsr);
	CHECK(ClampUpscaleFilter(4) == upscale_filter::Fsr);
	CHECK(ClampUpscaleFilter(1000) == upscale_filter::Fsr);
}

TEST_CASE("RcasSharpnessFromStops halves the strength per stop and clamps", "[upscale]")
{
	CHECK(RcasSharpnessFromStops(0.0f) == Approx(1.0f));
	CHECK(RcasSharpnessFromStops(1.0f) == Approx(0.5f));
	CHECK(RcasSharpnessFromStops(2.0f) == Approx(0.25f));

	CHECK(RcasSharpnessFromStops(-3.0f) == Approx(1.0f));
	CHECK(RcasSharpnessFromStops(10.0f) == Approx(0.25f));
}

TEST_CASE("UpscaleNeedsPass only when sizes differ and the filter is not bilinear", "[upscale]")
{
	SECTION("native resolution never needs a pass")
	{
		for (int32 f = 0; f < upscale_filter::Count_; ++f)
		{
			CHECK_FALSE(UpscaleNeedsPass(static_cast<upscale_filter::Type>(f), 1920, 1080, 1920, 1080));
		}
	}

	SECTION("bilinear is done by the final quad")
	{
		CHECK_FALSE(UpscaleNeedsPass(upscale_filter::Bilinear, 960, 540, 1920, 1080));
	}

	SECTION("other filters run below native resolution")
	{
		CHECK(UpscaleNeedsPass(upscale_filter::Point, 960, 540, 1920, 1080));
		CHECK(UpscaleNeedsPass(upscale_filter::Bicubic, 960, 540, 1920, 1080));
		CHECK(UpscaleNeedsPass(upscale_filter::Fsr, 960, 540, 1920, 1080));
		CHECK(UpscaleNeedsPass(upscale_filter::Fsr, 1920, 1079, 1920, 1080));
	}

	SECTION("empty sizes do nothing")
	{
		CHECK_FALSE(UpscaleNeedsPass(upscale_filter::Fsr, 0, 540, 1920, 1080));
		CHECK_FALSE(UpscaleNeedsPass(upscale_filter::Fsr, 960, 540, 0, 0));
	}
}
