// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graphics/graphics_device.h"
#include "graphics/sampler_state.h"
#include "graphics/volume_texture.h"
#include "null_device.h"

using namespace mmo;

TEST_CASE("Compute API defaults are safe no-ops on the null device", "[graphics_compute]")
{
	GraphicsDevice& device = test::EnsureNullDevice();

	// Backends without compute support return no resources; callers must treat nullptr as
	// "feature unavailable" rather than crash.
	CHECK(device.CreateVolumeTexture(4, 4, 4, VolumeFormat::R8, false) == nullptr);
	CHECK(device.CreateVolumeTexture(8, 8, 8, VolumeFormat::RGBA16F, true) == nullptr);

	SamplerDesc desc;
	desc.filter = SamplerFilter::ComparisonLinear;
	desc.address = SamplerAddress::Border;
	CHECK(device.CreateSamplerState(desc) == nullptr);

	device.Dispatch(1, 1, 1);
	device.ClearComputeBindings();
	SUCCEED("Dispatch and ClearComputeBindings returned without side effects");
}

TEST_CASE("SamplerDesc defaults describe a linear clamp sampler", "[graphics_compute]")
{
	const SamplerDesc desc;
	CHECK(desc.filter == SamplerFilter::Linear);
	CHECK(desc.address == SamplerAddress::Clamp);
	CHECK(desc.comparison == SamplerComparison::LessEqual);
	CHECK(desc.maxAnisotropy == 1u);
	CHECK(desc.borderColor[0] == Approx(0.0f));
	CHECK(desc.borderColor[3] == Approx(0.0f));
}
