// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "client_data/project.h"
#include "scene_graph/environment_controller.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_profile_proto.h"
#include "scene_graph/environment_state.h"

#include <memory>

using namespace mmo;

namespace
{
	std::shared_ptr<const EnvironmentProfile> makeProfile(const String& lut, const float transitionSeconds)
	{
		EnvironmentProfile profile = EnvironmentProfile::MakeDefault();
		profile.colorLut = lut;
		profile.transitionSeconds = transitionSeconds;
		return std::make_shared<const EnvironmentProfile>(profile);
	}
}

TEST_CASE("Colour grading defaults leave the image unchanged", "[environment]")
{
	const EnvironmentProfile profile = EnvironmentProfile::MakeDefault();
	CHECK(profile.colorLut.empty());
	CHECK(profile.saturation == Approx(1.0f));
	CHECK(profile.contrast == Approx(1.0f));
	CHECK(profile.colorFilter.x == Approx(1.0f));

	const EnvironmentState state = EvaluateEnvironment(profile, 0.5f);
	CHECK(state.colorLut.empty());
	CHECK(state.colorLutFrom.empty());
	CHECK(state.colorLutBlend == Approx(1.0f));
}

TEST_CASE("Colour grading loads clamped from the profile record", "[environment]")
{
	proto_client::EnvironmentProfile record;
	record.set_id(3);
	record.set_name("Graded");
	CHECK(record.saturation() == Approx(1.0f));
	CHECK(record.color_filter_b() == Approx(1.0f));

	record.set_color_lut("Textures/ColorGrading/Test.htex");
	record.set_saturation(9.0f);
	record.set_contrast(-1.0f);
	record.set_color_filter_r(0.5f);
	record.set_color_filter_g(3.0f);

	const EnvironmentProfile profile = LoadEnvironmentProfile(record);
	CHECK(profile.colorLut == "Textures/ColorGrading/Test.htex");
	CHECK(profile.saturation == Approx(2.0f));
	CHECK(profile.contrast == Approx(0.0f));
	CHECK(profile.colorFilter.x == Approx(0.5f));
	CHECK(profile.colorFilter.y == Approx(2.0f));
	CHECK(profile.colorFilter.z == Approx(1.0f));
}

TEST_CASE("Colour grading sliders blend linearly", "[environment]")
{
	EnvironmentState a;
	EnvironmentState b;
	a.saturation = 1.0f;
	b.saturation = 1.4f;
	a.contrast = 0.8f;
	b.contrast = 1.2f;
	a.colorFilter = Vector3(1.0f, 1.0f, 1.0f);
	b.colorFilter = Vector3(1.2f, 1.0f, 0.8f);

	const EnvironmentState half = LerpEnvironment(a, b, 0.5f);
	CHECK(half.saturation == Approx(1.2f));
	CHECK(half.contrast == Approx(1.0f));
	CHECK(half.colorFilter.x == Approx(1.1f));
	CHECK(half.colorFilter.z == Approx(0.9f));
}

TEST_CASE("The controller fades from the previous zone's LUT to the new one", "[environment]")
{
	EnvironmentController controller;
	controller.SetTarget(makeProfile("A.htex", 2.0f), true);
	CHECK(controller.GetState().colorLut == "A.htex");
	CHECK(controller.GetState().colorLutFrom.empty());
	CHECK(controller.GetState().colorLutBlend == Approx(1.0f));

	controller.SetTarget(makeProfile("B.htex", 2.0f), false);
	controller.Update(0.5f, 0.5f);
	CHECK(controller.GetState().colorLut == "B.htex");
	CHECK(controller.GetState().colorLutFrom == "A.htex");
	CHECK(controller.GetState().colorLutBlend == Approx(0.25f));

	controller.Update(2.0f, 0.5f);
	CHECK(controller.GetState().colorLut == "B.htex");
	CHECK(controller.GetState().colorLutBlend == Approx(1.0f));
}

TEST_CASE("With three profiles blending the strongest other LUT is kept", "[environment]")
{
	EnvironmentController controller;
	controller.SetTarget(makeProfile("A.htex", 4.0f), true);
	controller.SetTarget(makeProfile("B.htex", 4.0f), false);
	controller.Update(3.0f, 0.5f);           // B at 0.75, A at 0.25
	controller.SetTarget(makeProfile("C.htex", 4.0f), false);
	controller.Update(0.4f, 0.5f);           // C at 0.1; B is the strongest other entry

	CHECK(controller.GetState().colorLut == "C.htex");
	CHECK(controller.GetState().colorLutFrom == "B.htex");
	CHECK(controller.GetState().colorLutBlend == Approx(0.1f).margin(1e-3));
}
