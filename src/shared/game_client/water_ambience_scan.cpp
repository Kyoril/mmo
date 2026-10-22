// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "water_ambience_scan.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		/// Running sums of one profile while scanning.
		struct Accumulator
		{
			float weight = 0.0f;
			Vector3 direction;
			float speed = 0.0f;
		};

		bool IsLand(const WaterSample& neighbour, const float surfaceY)
		{
			return !neighbour.hasWater || (neighbour.hasGround && neighbour.groundY >= surfaceY);
		}

		bool IsShore(const IWaterAmbienceSource& source, const float x, const float z, const float surfaceY)
		{
			const float spacing = water_ambience::GridSpacing;
			return IsLand(source.Sample(x - spacing, z), surfaceY)
				|| IsLand(source.Sample(x + spacing, z), surfaceY)
				|| IsLand(source.Sample(x, z - spacing), surfaceY)
				|| IsLand(source.Sample(x, z + spacing), surfaceY);
		}

		float SnapToCentre(const int64 cell)
		{
			return (static_cast<float>(cell) + 0.5f) * water_ambience::GridSpacing;
		}

		int64 CellOf(const float coordinate)
		{
			return static_cast<int64>(std::floor(coordinate / water_ambience::GridSpacing));
		}
	}

	float WaterAmbienceField::GetFocus() const
	{
		return direction.GetLength();
	}

	std::vector<WaterAmbienceField> ScanWaterAmbience(const IWaterAmbienceSource& source, const Vector3& ear, const std::vector<WaterAmbienceProfile>& profiles)
	{
		std::vector<WaterAmbienceField> fields(profiles.size());
		std::vector<Accumulator> sums(profiles.size());

		float maxRange = 0.0f;
		for (size_t i = 0; i < profiles.size(); ++i)
		{
			fields[i].waterType = profiles[i].waterType;
			maxRange = std::max(maxRange, profiles[i].range);
		}

		if (maxRange <= 0.0f)
		{
			return fields;
		}

		const int64 minCellX = CellOf(ear.x - maxRange);
		const int64 maxCellX = CellOf(ear.x + maxRange);
		const int64 minCellZ = CellOf(ear.z - maxRange);
		const int64 maxCellZ = CellOf(ear.z + maxRange);

		for (int64 cellX = minCellX; cellX <= maxCellX; ++cellX)
		{
			const float x = SnapToCentre(cellX);
			const float dx = x - ear.x;

			for (int64 cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ)
			{
				const float z = SnapToCentre(cellZ);
				const float dz = z - ear.z;
				const float horizontalDistance = std::sqrt(dx * dx + dz * dz);
				if (horizontalDistance > maxRange)
				{
					continue;
				}

				const WaterSample sample = source.Sample(x, z);
				if (!sample.hasWater)
				{
					continue;
				}

				const Vector3 toWater(dx, sample.surfaceY - ear.y, dz);
				const float distance = toWater.GetLength();

				// The shore check costs four more samples, so it runs at most once per point.
				bool shoreChecked = false;
				bool isShore = false;

				for (size_t i = 0; i < profiles.size(); ++i)
				{
					const WaterAmbienceProfile& profile = profiles[i];
					if (profile.waterType != sample.waterType || profile.range <= 0.0f)
					{
						continue;
					}

					if (horizontalDistance > profile.range || distance >= profile.range)
					{
						continue;
					}

					if (profile.shoreOnly)
					{
						if (!shoreChecked)
						{
							isShore = IsShore(source, x, z, sample.surfaceY);
							shoreChecked = true;
						}

						if (!isShore)
						{
							continue;
						}
					}

					const float falloff = 1.0f - distance / profile.range;
					const float weight = falloff * falloff;

					Accumulator& sum = sums[i];
					sum.weight += weight;
					if (distance > 0.0f)
					{
						sum.direction += toWater * (weight / distance);
					}
					sum.speed += weight * sample.flowSpeed;
				}
			}
		}

		for (size_t i = 0; i < profiles.size(); ++i)
		{
			const Accumulator& sum = sums[i];
			if (sum.weight <= 0.0f)
			{
				continue;
			}

			WaterAmbienceField& field = fields[i];
			field.level = std::min(1.0f, sum.weight / water_ambience::LevelSaturationWeight);
			field.direction = sum.direction / sum.weight;
			field.speed = sum.speed / sum.weight;
		}

		return fields;
	}
}
