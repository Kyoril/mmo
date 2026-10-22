// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <cstdio>
#include <optional>
#include <sstream>

namespace mmo
{
	/// @brief A camera placement in the world editor that can be written down and restored exactly,
	///        so the same shot can be taken again after the world changed (for example to compare a
	///        zone against concept art).
	struct EditorViewpoint final
	{
		/// @brief Eye position in world units.
		Vector3 position;

		/// @brief Heading in degrees. 0 looks north (-Z), 90 looks west (-X), 180 south, 270 east.
		float yawDegrees = 0.0f;

		/// @brief Elevation in degrees. Negative looks down.
		float pitchDegrees = -35.0f;

		/// @brief Vertical field of view in degrees.
		float fovDegrees = 45.0f;

		/// @brief Normalized time of day (0 = midnight, 0.5 = noon), or negative to keep the current one.
		float timeOfDay = -1.0f;

		/// @brief Formats the viewpoint as "x,y,z,yaw,pitch,fov[,time]", the form Parse reads.
		[[nodiscard]] String ToString() const
		{
			char buffer[160];
			if (timeOfDay >= 0.0f)
			{
				std::snprintf(buffer, sizeof(buffer), "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.4f",
					position.x, position.y, position.z, yawDegrees, pitchDegrees, fovDegrees, timeOfDay);
			}
			else
			{
				std::snprintf(buffer, sizeof(buffer), "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
					position.x, position.y, position.z, yawDegrees, pitchDegrees, fovDegrees);
			}
			return buffer;
		}

		/// @brief Reads "x,y,z,yaw,pitch[,fov[,time]]". Missing optional values keep their defaults.
		/// @return The viewpoint, or nothing if fewer than five numbers could be read.
		[[nodiscard]] static std::optional<EditorViewpoint> Parse(const String& text)
		{
			float values[7] = {};
			int count = 0;

			std::istringstream stream(text);
			String token;
			while (count < 7 && std::getline(stream, token, ','))
			{
				std::istringstream number(token);
				if (!(number >> values[count]))
				{
					return std::nullopt;
				}
				++count;
			}

			if (count < 5)
			{
				return std::nullopt;
			}

			EditorViewpoint viewpoint;
			viewpoint.position = Vector3(values[0], values[1], values[2]);
			viewpoint.yawDegrees = values[3];
			viewpoint.pitchDegrees = values[4];
			if (count > 5)
			{
				viewpoint.fovDegrees = values[5];
			}
			if (count > 6)
			{
				viewpoint.timeOfDay = values[6];
			}
			return viewpoint;
		}
	};
}
