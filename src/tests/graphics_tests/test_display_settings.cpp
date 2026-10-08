// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graphics/display_settings.h"

using namespace mmo;

namespace
{
	constexpr uint64 MiB = 1024ull * 1024ull;

	GpuInfo Dedicated(const uint32 vendor, const uint64 vramMiB)
	{
		GpuInfo gpu;
		gpu.vendorId = vendor;
		gpu.deviceId = 0x1234;
		gpu.dedicatedVideoMemory = vramMiB * MiB;
		return gpu;
	}

	GpuInfo Integrated(const uint32 vendor, const uint32 device, const uint64 carveOutMiB)
	{
		GpuInfo gpu = Dedicated(vendor, carveOutMiB);
		gpu.deviceId = device;
		gpu.integrated = true;
		return gpu;
	}
}

TEST_CASE("Graphics preset recommendation follows the GPU class", "[display_settings]")
{
	SECTION("Software rasterizers get Low")
	{
		GpuInfo warp = Dedicated(gpu_vendor::Microsoft, 0);
		CHECK(RecommendGraphicsPreset(warp, 1920, 1080) == graphics_preset::Low);

		GpuInfo software = Dedicated(gpu_vendor::Nvidia, 8192);
		software.software = true;
		CHECK(RecommendGraphicsPreset(software, 1920, 1080) == graphics_preset::Low);
	}

	SECTION("Unknown adapters get Medium")
	{
		CHECK(RecommendGraphicsPreset(GpuInfo{}, 1920, 1080) == graphics_preset::Medium);
	}

	SECTION("Integrated GPUs get Low unless they are known to be strong")
	{
		CHECK(RecommendGraphicsPreset(Integrated(gpu_vendor::Intel, 0x9A49, 128), 1920, 1080) == graphics_preset::Low);
		CHECK(RecommendGraphicsPreset(Integrated(gpu_vendor::Amd, 0x150E, 512), 1920, 1080) == graphics_preset::Medium);
	}

	SECTION("A large firmware carve-out does not make an integrated GPU look dedicated")
	{
		CHECK(RecommendGraphicsPreset(Integrated(gpu_vendor::Amd, 0x150E, 4096), 1920, 1080) == graphics_preset::Medium);
	}

	SECTION("Unflagged integrated GPUs are recognized by their tiny carve-out")
	{
		CHECK(RecommendGraphicsPreset(Dedicated(gpu_vendor::Intel, 128), 1920, 1080) == graphics_preset::Low);
	}

	SECTION("Dedicated GPUs are graded by video memory")
	{
		CHECK(RecommendGraphicsPreset(Dedicated(gpu_vendor::Nvidia, 1024), 1920, 1080) == graphics_preset::Low);
		CHECK(RecommendGraphicsPreset(Dedicated(gpu_vendor::Nvidia, 2048), 1920, 1080) == graphics_preset::Medium);
		CHECK(RecommendGraphicsPreset(Dedicated(gpu_vendor::Amd, 4096), 1920, 1080) == graphics_preset::High);
		CHECK(RecommendGraphicsPreset(Dedicated(gpu_vendor::Nvidia, 12288), 1920, 1080) == graphics_preset::Ultra);
	}

	SECTION("Displays above 1440p cost one step, but never below Low")
	{
		CHECK(RecommendGraphicsPreset(Dedicated(gpu_vendor::Nvidia, 12288), 2560, 1440) == graphics_preset::Ultra);
		CHECK(RecommendGraphicsPreset(Dedicated(gpu_vendor::Nvidia, 12288), 3840, 2160) == graphics_preset::High);
		CHECK(RecommendGraphicsPreset(Integrated(gpu_vendor::Intel, 0x9A49, 128), 3840, 2160) == graphics_preset::Low);
	}
}

TEST_CASE("Frame pacer limits the frame rate", "[display_settings]")
{
	using Clock = FramePacer::Clock;
	using namespace std::chrono;

	FramePacer pacer;
	const Clock::time_point start{};

	SECTION("Unlimited never waits")
	{
		CHECK(pacer.NextFrameStart(start, 0.0f) == start);
		CHECK(pacer.NextFrameStart(start + milliseconds(1), 0.0f) == start + milliseconds(1));
	}

	SECTION("Fast frames wait for the interval, measured from the previous deadline")
	{
		CHECK(pacer.NextFrameStart(start, 50.0f) == start);
		CHECK(pacer.NextFrameStart(start + milliseconds(5), 50.0f) == start + milliseconds(20));
		CHECK(pacer.NextFrameStart(start + milliseconds(26), 50.0f) == start + milliseconds(40));
	}

	SECTION("A slow frame starts the next one at once without rushing the ones after it")
	{
		pacer.NextFrameStart(start, 50.0f);
		CHECK(pacer.NextFrameStart(start + milliseconds(100), 50.0f) == start + milliseconds(100));
		CHECK(pacer.NextFrameStart(start + milliseconds(101), 50.0f) == start + milliseconds(120));
	}
}

TEST_CASE("Dynamic resolution holds the target frame rate", "[display_settings]")
{
	DynamicResolutionController controller;

	// Feeds a number of seconds worth of 10 ms frames with the given GPU time.
	const auto feed = [&controller](const double seconds, const double gpuMs)
	{
		bool changed = false;
		for (double t = 0.0; t < seconds; t += 0.01)
		{
			changed |= controller.Update(0.01, gpuMs);
		}
		return changed;
	};

	SECTION("Disabled keeps the full scale")
	{
		CHECK_FALSE(feed(5.0, 100.0));
		CHECK(controller.GetScale() == 1.0f);
	}

	SECTION("A slow GPU lowers the scale step by step down to the minimum")
	{
		controller.SetTargetFps(60.0f);
		CHECK(feed(1.1, 30.0));
		CHECK(controller.GetScale() == Approx(0.9f));

		feed(20.0, 30.0);
		CHECK(controller.GetScale() == Approx(DynamicResolutionController::MinScale));
	}

	SECTION("Headroom raises the scale back, but only if the bigger frame fits")
	{
		controller.SetTargetFps(60.0f);
		feed(1.1, 30.0);
		REQUIRE(controller.GetScale() == Approx(0.9f));

		// 13 ms is headroom at 0.9, but predicts 16 ms at full scale, above 90% of the 16.7 ms budget: stay.
		feed(5.0, 13.0);
		CHECK(controller.GetScale() == Approx(0.9f));

		feed(2.1, 8.0);
		CHECK(controller.GetScale() == 1.0f);
	}

	SECTION("Disabling restores the full scale at once")
	{
		controller.SetTargetFps(60.0f);
		feed(1.1, 30.0);
		controller.SetTargetFps(0.0f);
		CHECK(controller.Update(0.01, 30.0));
		CHECK(controller.GetScale() == 1.0f);
	}
}
