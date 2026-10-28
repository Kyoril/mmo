// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_camera_path.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		Vector3 catmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float u)
		{
			const float u2 = u * u;
			const float u3 = u2 * u;
			return (p1 * 2.0f
				+ (p2 - p0) * u
				+ (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * u2
				+ (p1 * 3.0f - p0 - p2 * 3.0f + p3) * u3) * 0.5f;
		}
	}

	GlueCameraKey EvaluateGluePath(const std::vector<GlueCameraKey>& keys, float t)
	{
		if (keys.empty())
		{
			return GlueCameraKey{};
		}

		if (keys.size() == 1)
		{
			return keys.front();
		}

		t = std::clamp(t, 0.0f, 1.0f);
		const float eased = t * t * (3.0f - 2.0f * t);

		const int segments = static_cast<int>(keys.size()) - 1;
		const float scaled = eased * static_cast<float>(segments);
		const int segment = std::min(static_cast<int>(scaled), segments - 1);
		const float u = scaled - static_cast<float>(segment);

		const auto key = [&keys](int i) -> const GlueCameraKey&
		{
			return keys[static_cast<size_t>(std::clamp(i, 0, static_cast<int>(keys.size()) - 1))];
		};

		const GlueCameraKey& k0 = key(segment - 1);
		const GlueCameraKey& k1 = key(segment);
		const GlueCameraKey& k2 = key(segment + 1);
		const GlueCameraKey& k3 = key(segment + 2);

		GlueCameraKey result;
		result.position = catmullRom(k0.position, k1.position, k2.position, k3.position, u);
		result.lookAt = catmullRom(k0.lookAt, k1.lookAt, k2.lookAt, k3.lookAt, u);
		return result;
	}
}
