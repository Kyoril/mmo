// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "graphics/float_curve.h"

#include <cmath>

using namespace mmo;

namespace
{
	bool Near(float a, float b, float tol = 1e-4f)
	{
		return std::fabs(a - b) <= tol;
	}
}

TEST_CASE("FloatCurve default spans 0 to 1", "[float_curve]")
{
	FloatCurve curve;
	CHECK(Near(curve.Evaluate(0.0f), 0.0f));
	CHECK(Near(curve.Evaluate(1.0f), 1.0f));
	// Monotonic increasing in the middle.
	CHECK(curve.Evaluate(0.25f) < curve.Evaluate(0.75f));
}

TEST_CASE("FloatCurve start/end constructor", "[float_curve]")
{
	FloatCurve curve(2.0f, 0.5f);
	CHECK(Near(curve.Evaluate(0.0f), 2.0f));
	CHECK(Near(curve.Evaluate(1.0f), 0.5f));
}

TEST_CASE("FloatCurve clamps outside the key range", "[float_curve]")
{
	FloatCurve curve(1.0f, 3.0f);
	CHECK(Near(curve.Evaluate(-5.0f), 1.0f));
	CHECK(Near(curve.Evaluate(5.0f), 3.0f));
}

TEST_CASE("FloatCurve AddKey inserts in time order and is sampled", "[float_curve]")
{
	FloatCurve curve(0.0f, 0.0f);
	curve.AddKey(0.5f, 1.0f);
	REQUIRE(curve.GetKeyCount() == 3);
	// Peak in the middle should exceed the endpoints.
	CHECK(curve.Evaluate(0.5f) > curve.Evaluate(0.0f));
	CHECK(curve.Evaluate(0.5f) > curve.Evaluate(1.0f));
}

TEST_CASE("FloatCurve keeps at least two keys after removal", "[float_curve]")
{
	FloatCurve curve(0.0f, 1.0f);
	curve.AddKey(0.5f, 0.5f);
	REQUIRE(curve.GetKeyCount() == 3);

	// Remove down toward the minimum; the curve must never drop below two keys.
	curve.RemoveKey(0);
	curve.RemoveKey(0);
	curve.RemoveKey(0);
	CHECK(curve.GetKeyCount() >= 2);
}

TEST_CASE("FloatCurve vector constructor sorts keys", "[float_curve]")
{
	std::vector<FloatKey> keys = {
		FloatKey(1.0f, 5.0f),
		FloatKey(0.0f, 1.0f),
		FloatKey(0.5f, 3.0f)
	};
	FloatCurve curve(std::move(keys));
	REQUIRE(curve.GetKeyCount() == 3);
	CHECK(curve.GetKey(0).time <= curve.GetKey(1).time);
	CHECK(curve.GetKey(1).time <= curve.GetKey(2).time);
	CHECK(Near(curve.Evaluate(0.0f), 1.0f));
	CHECK(Near(curve.Evaluate(1.0f), 5.0f));
}
