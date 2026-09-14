// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "scene_graph/environment_state.h"
#include "scene_graph/wind_simulation.h"

#include <cmath>

using namespace mmo;

namespace
{
	EnvironmentState makeWind(const float degrees, const float speed, const float gustiness)
	{
		EnvironmentState state;
		state.windDirection = WindDirectionFromDegrees(degrees);
		state.windSpeed = speed;
		state.windGustiness = gustiness;
		state.fogNoiseSize = 40.0f;
		state.fogNoiseAmount = 0.7f;
		return state;
	}

	/// Shortest signed difference of two offsets wrapped to [0, 1).
	float wrappedDelta(const float from, const float to)
	{
		float delta = to - from;
		delta -= std::round(delta);
		return delta;
	}
}

TEST_CASE("Gust noise is smooth and bounded", "[wind]")
{
	float previous = WindGustNoise(0.0);
	for (int i = 1; i < 2000; ++i)
	{
		const double time = static_cast<double>(i) * 0.01;
		const float value = WindGustNoise(time);
		CHECK(value >= -1.0f);
		CHECK(value <= 1.0f);
		CHECK(std::abs(value - previous) < 0.1f);
		previous = value;
	}
}

TEST_CASE("Zero wind speed leaves the noise offset still", "[wind]")
{
	WindSimulation simulation;
	const EnvironmentState calm = makeWind(90.0f, 0.0f, 1.0f);

	simulation.Update(0.1f, calm);
	const float x = simulation.GetState().noiseOffsetX;
	const float z = simulation.GetState().noiseOffsetZ;

	for (int i = 0; i < 100; ++i)
	{
		simulation.Update(0.1f, calm);
	}

	CHECK(simulation.GetState().noiseOffsetX == Approx(x));
	CHECK(simulation.GetState().noiseOffsetZ == Approx(z));
	CHECK(simulation.GetState().speed == Approx(0.0f));
}

TEST_CASE("Gusts keep speed and direction within their bounds", "[wind]")
{
	WindSimulation simulation;
	const EnvironmentState gusty = makeWind(0.0f, 10.0f, 0.5f);

	for (int i = 0; i < 3000; ++i)
	{
		simulation.Update(0.05f, gusty);
		const WindState& state = simulation.GetState();

		CHECK(state.speed >= 10.0f * (1.0f - WindSimulation::MaxGustSpeedFactor * 0.5f) - 1e-3f);
		CHECK(state.speed <= 10.0f * (1.0f + WindSimulation::MaxGustSpeedFactor * 0.5f) + 1e-3f);

		const float angleDegrees = std::acos(std::clamp(state.direction.z, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
		CHECK(angleDegrees <= WindSimulation::MaxGustAngleDegrees * 0.5f + 0.01f);
		CHECK(state.direction.y == Approx(0.0f));
	}
}

TEST_CASE("Noise offset moves continuously across direction and speed changes", "[wind]")
{
	WindSimulation simulation;
	constexpr float dt = 1.0f / 60.0f;

	float previousX = simulation.GetState().noiseOffsetX;
	float previousZ = simulation.GetState().noiseOffsetZ;

	for (int i = 0; i < 1200; ++i)
	{
		// Switch direction and speed abruptly every 200 frames.
		const int phase = i / 200;
		const EnvironmentState wind = makeWind(static_cast<float>(phase) * 73.0f, 2.0f + static_cast<float>(phase) * 3.0f, 0.3f);
		simulation.Update(dt, wind);

		const WindState& state = simulation.GetState();
		const float maxStepMetres = wind.windSpeed * (1.0f + WindSimulation::MaxGustSpeedFactor) * dt + 1e-3f;
		CHECK(std::abs(wrappedDelta(previousX, state.noiseOffsetX)) * state.noiseSize <= maxStepMetres);
		CHECK(std::abs(wrappedDelta(previousZ, state.noiseOffsetZ)) * state.noiseSize <= maxStepMetres);

		CHECK(state.noiseOffsetX >= 0.0f);
		CHECK(state.noiseOffsetX < 1.0f);
		CHECK(state.noiseOffsetZ >= 0.0f);
		CHECK(state.noiseOffsetZ < 1.0f);

		previousX = state.noiseOffsetX;
		previousZ = state.noiseOffsetZ;
	}
}

TEST_CASE("Wind moves the noise offset toward the wind direction", "[wind]")
{
	WindSimulation simulation;
	const EnvironmentState eastward = makeWind(90.0f, 4.0f, 0.0f);

	float previousX = simulation.GetState().noiseOffsetX;
	simulation.Update(1.0f, eastward);

	// 4 m/s for one second with a 40 m noise size advances x by 0.1.
	CHECK(wrappedDelta(previousX, simulation.GetState().noiseOffsetX) == Approx(0.1f).margin(1e-4));
	CHECK(simulation.GetState().noiseSize == Approx(40.0f));
	CHECK(simulation.GetState().noiseAmount == Approx(0.7f));
}

TEST_CASE("Reset clears the accumulated offset", "[wind]")
{
	WindSimulation simulation;
	simulation.Update(3.0f, makeWind(45.0f, 5.0f, 0.0f));
	simulation.Reset();

	CHECK(simulation.GetState().noiseOffsetX == Approx(0.0f));
	CHECK(simulation.GetState().noiseOffsetZ == Approx(0.0f));
}
