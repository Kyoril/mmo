// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "wind_simulation.h"

#include "graphics/global_shader_parameters.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		constexpr double gustFrequency = 0.35;
		constexpr double directionGustTimeOffset = 17.3;
		constexpr float degreesToRadians = 3.14159265358979f / 180.0f;

		float hashToSigned(const int64 cell)
		{
			uint32 h = static_cast<uint32>(cell) * 0x9E3779B1u ^ static_cast<uint32>(static_cast<uint64>(cell) >> 32) * 0x85EBCA6Bu;
			h ^= h >> 16;
			h *= 0x7FEB352Du;
			h ^= h >> 15;
			h *= 0x846CA68Bu;
			h ^= h >> 16;
			return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu) * 2.0f - 1.0f;
		}

		float wrap01(const double value)
		{
			const float wrapped = static_cast<float>(value - std::floor(value));
			return wrapped >= 1.0f ? 0.0f : wrapped;
		}
	}

	float WindGustNoise(const double time)
	{
		const double scaled = time * gustFrequency;
		const double cell = std::floor(scaled);
		const float t = static_cast<float>(scaled - cell);
		const float fade = t * t * (3.0f - 2.0f * t);

		const int64 index = static_cast<int64>(cell);
		const float a = hashToSigned(index);
		const float b = hashToSigned(index + 1);
		return a + (b - a) * fade;
	}

	void WindSimulation::Update(const float deltaSeconds, const EnvironmentState& environment)
	{
		m_time += static_cast<double>(deltaSeconds);

		const float gustiness = std::clamp(environment.windGustiness, 0.0f, 1.0f);
		const float speed = std::max(0.0f, environment.windSpeed * (1.0f + gustiness * MaxGustSpeedFactor * WindGustNoise(m_time)));

		// Positive angles turn from +Z toward +X, matching the clockwise-from-north convention.
		const float angle = MaxGustAngleDegrees * gustiness * WindGustNoise(m_time + directionGustTimeOffset) * degreesToRadians;
		const float cosAngle = std::cos(angle);
		const float sinAngle = std::sin(angle);
		const Vector3& base = environment.windDirection;
		const Vector3 direction(base.x * cosAngle + base.z * sinAngle, 0.0f, base.z * cosAngle - base.x * sinAngle);

		m_offsetX += static_cast<double>(direction.x) * speed * deltaSeconds;
		m_offsetZ += static_cast<double>(direction.z) * speed * deltaSeconds;

		const float noiseSize = std::max(environment.fogNoiseSize, 5.0f);

		m_state.direction = direction;
		m_state.speed = speed;
		m_state.noiseSize = noiseSize;
		m_state.noiseAmount = std::clamp(environment.fogNoiseAmount, 0.0f, 1.0f);
		m_state.noiseOffsetX = wrap01(m_offsetX / noiseSize);
		m_state.noiseOffsetZ = wrap01(m_offsetZ / noiseSize);
	}

	void WindSimulation::Reset()
	{
		m_time = 0.0;
		m_offsetX = 0.0;
		m_offsetZ = 0.0;
		m_state.noiseOffsetX = 0.0f;
		m_state.noiseOffsetZ = 0.0f;
	}

	void PublishWindShaderParameters(const WindState& state)
	{
		GlobalShaderParameters::Get().SetVector("WindDirection", Vector4(state.direction.x, state.direction.y, state.direction.z, state.speed));
	}
}
