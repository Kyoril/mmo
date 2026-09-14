// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/environment_controller.h"
#include "scene_graph/environment_state.h"

#include <memory>

using namespace mmo;

namespace
{
	/// A profile whose every curve is one constant colour and whose exposure is a marker value,
	/// so a blended state reveals the weights that produced it.
	std::shared_ptr<const EnvironmentProfile> MakeFlatProfile(const float value, const float transitionSeconds)
	{
		EnvironmentProfile profile = EnvironmentProfile::MakeDefault();
		const ColorCurve flat = MakeEnvironmentCurve({ { 0.0f, Vector4(value, value, value, value) }, { 1.0f, Vector4(value, value, value, value) } });
		profile.skyHorizon = flat;
		profile.skyZenith = flat;
		profile.clouds = flat;
		profile.ambient = flat;
		profile.sun = flat;
		profile.moon = flat;
		profile.fog = flat;
		profile.sunScatter = flat;
		profile.exposure = value;
		profile.transitionSeconds = transitionSeconds;
		return std::make_shared<const EnvironmentProfile>(std::move(profile));
	}

	float SumOfWeights(const EnvironmentController& controller, std::initializer_list<const EnvironmentProfile*> profiles)
	{
		float sum = 0.0f;
		for (const EnvironmentProfile* profile : profiles)
		{
			sum += controller.GetWeight(profile);
		}
		return sum;
	}
}

TEST_CASE("EvaluateEnvironment copies curve values and fixed values", "[environment]")
{
	const auto profile = MakeFlatProfile(0.25f, 3.0f);
	const EnvironmentState state = EvaluateEnvironment(*profile, 0.4f);

	CHECK(state.skyHorizon.x == Approx(0.25f));
	CHECK(state.ambient.y == Approx(0.25f));
	CHECK(state.sunColor.z == Approx(0.25f));
	CHECK(state.sunIntensity == Approx(0.25f));
	CHECK(state.moonIntensity == Approx(0.25f));
	CHECK(state.timeOfDay.fogTint[0] == Approx(0.25f));
	CHECK(state.timeOfDay.densityMultiplier == Approx(0.25f));
	CHECK(state.timeOfDay.sunScatterColor[2] == Approx(0.25f));
	CHECK(state.timeOfDay.shaftMultiplier == Approx(0.25f));
	CHECK(state.exposure == Approx(0.25f));
	CHECK(state.atmosphere.density == Approx(profile->atmosphere.density));
}

TEST_CASE("LerpEnvironment returns the inputs at its endpoints", "[environment]")
{
	const EnvironmentState a = EvaluateEnvironment(*MakeFlatProfile(0.0f, 3.0f), 0.5f);
	const EnvironmentState b = EvaluateEnvironment(*MakeFlatProfile(1.0f, 3.0f), 0.5f);

	CHECK(LerpEnvironment(a, b, 0.0f).exposure == Approx(0.0f));
	CHECK(LerpEnvironment(a, b, 1.0f).exposure == Approx(1.0f));
	CHECK(LerpEnvironment(a, b, 0.25f).skyZenith.x == Approx(0.25f));
	CHECK(LerpEnvironment(a, b, 0.25f).timeOfDay.fogTint[1] == Approx(0.25f));
}

TEST_CASE("Controller starts on the Default profile", "[environment]")
{
	EnvironmentController controller;
	controller.Update(0.016f, 0.5f);

	const EnvironmentState expected = EvaluateEnvironment(*EnvironmentProfile::GetDefault(), 0.5f);
	CHECK(controller.GetBlendEntryCount() == 1);
	CHECK(controller.GetState().skyHorizon.x == Approx(expected.skyHorizon.x));
}

TEST_CASE("A fade takes the incoming profile's transition time and weights sum to one", "[environment]")
{
	const auto from = MakeFlatProfile(0.0f, 1.0f);
	const auto to = MakeFlatProfile(1.0f, 2.0f);

	EnvironmentController controller;
	controller.SetTarget(from, true);
	controller.SetTarget(to, false);

	controller.Update(0.5f, 0.5f);
	CHECK(controller.GetWeight(to.get()) == Approx(0.25f));
	CHECK(SumOfWeights(controller, { from.get(), to.get() }) == Approx(1.0f));
	CHECK(controller.GetState().exposure == Approx(0.25f));

	controller.Update(1.5f, 0.5f);
	CHECK(controller.GetWeight(to.get()) == Approx(1.0f));
	CHECK(controller.GetBlendEntryCount() == 1);
	CHECK(controller.GetState().exposure == Approx(1.0f));
}

TEST_CASE("Returning to a fading profile continues from its current weight", "[environment]")
{
	const auto a = MakeFlatProfile(0.0f, 4.0f);
	const auto b = MakeFlatProfile(1.0f, 4.0f);

	EnvironmentController controller;
	controller.SetTarget(a, true);
	controller.SetTarget(b, false);
	controller.Update(1.0f, 0.5f);
	REQUIRE(controller.GetWeight(a.get()) == Approx(0.75f));

	controller.SetTarget(a, false);
	CHECK(controller.GetWeight(a.get()) == Approx(0.75f));

	controller.Update(0.5f, 0.5f);
	CHECK(controller.GetWeight(a.get()) == Approx(0.875f));
	CHECK(SumOfWeights(controller, { a.get(), b.get() }) == Approx(1.0f));
}

TEST_CASE("Setting the current target again changes nothing", "[environment]")
{
	const auto a = MakeFlatProfile(0.0f, 4.0f);
	const auto b = MakeFlatProfile(1.0f, 4.0f);

	EnvironmentController controller;
	controller.SetTarget(a, true);
	controller.SetTarget(b, false);
	controller.Update(1.0f, 0.5f);

	controller.SetTarget(b, false);
	CHECK(controller.GetWeight(b.get()) == Approx(0.25f));
	CHECK(controller.GetBlendEntryCount() == 2);
}

TEST_CASE("Immediate and zero-length transitions snap", "[environment]")
{
	const auto a = MakeFlatProfile(0.0f, 4.0f);
	const auto snap = MakeFlatProfile(1.0f, 0.0f);

	EnvironmentController controller;
	controller.SetTarget(a, true);
	CHECK(controller.GetState().exposure == Approx(0.0f));
	CHECK(controller.GetBlendEntryCount() == 1);

	controller.SetTarget(snap, false);
	CHECK(controller.GetBlendEntryCount() == 1);
	CHECK(controller.GetState().exposure == Approx(1.0f));
}

TEST_CASE("The blend list never exceeds its capacity", "[environment]")
{
	EnvironmentController controller;
	std::vector<std::shared_ptr<const EnvironmentProfile>> profiles;
	for (int i = 0; i < 8; ++i)
	{
		profiles.push_back(MakeFlatProfile(static_cast<float>(i) / 8.0f, 10.0f));
		controller.SetTarget(profiles.back(), false);
		controller.Update(0.5f, 0.5f);
		CHECK(controller.GetBlendEntryCount() <= EnvironmentController::MaxBlendEntries);
	}

	// The starting Default keeps the highest weight, so it is never the lowest-weight entry that
	// gets evicted: include it in the sum.
	float sum = controller.GetWeight(EnvironmentProfile::GetDefault().get());
	for (const auto& profile : profiles)
	{
		sum += controller.GetWeight(profile.get());
	}
	CHECK(sum == Approx(1.0f));
}

TEST_CASE("A null target means the Default profile", "[environment]")
{
	const auto a = MakeFlatProfile(0.0f, 4.0f);

	EnvironmentController controller;
	controller.SetTarget(a, true);
	controller.SetTarget(nullptr, true);
	CHECK(controller.GetWeight(EnvironmentProfile::GetDefault().get()) == Approx(1.0f));
}
