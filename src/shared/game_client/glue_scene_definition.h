// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "glue_camera_director.h"
#include "plaza_roster.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace mmo
{
	/// A place on the plaza where a character stands.
	struct GlueSpot
	{
		/// World position.
		Vector3 position;

		/// Facing in degrees.
		float facingDegrees = 0.0f;

		/// Pose / animation name.
		String pose;

		/// Optional camera framing for this spot. Default-constructed when absent.
		GlueCameraKey camera;
	};

	/// Data-driven description of the glue (login / character select) scene.
	struct GlueSceneDefinition
	{
		/// World (map) name.
		String world;

		/// Terrain pages (x, y) to load for the gate view.
		std::vector<std::pair<uint8, uint8>> gatePages;

		/// Terrain pages (x, y) to load for the plaza view.
		std::vector<std::pair<uint8, uint8>> plazaPages;

		/// Environment profile id.
		uint32 environmentProfile = 0;

		/// Camera path from the gate to the plaza.
		std::vector<GlueCameraKey> flightPath;

		/// Duration of a full flight in seconds.
		float flightSeconds = 0.0f;

		/// The front (selected character) spot.
		GlueSpot front;

		/// The fixed plaza spots.
		std::array<GlueSpot, PlazaSpotCount> spots;

		/// Where characters enter the plaza from.
		Vector3 entrance;
	};

	/// Parses a scene definition. Returns nullopt if anything required is missing or
	/// malformed, if the flight path has fewer than 2 keys, or there are not exactly
	/// PlazaSpotCount spots. The camera of front and spots is optional.
	std::optional<GlueSceneDefinition> ParseGlueSceneDefinition(const String& json);
}
