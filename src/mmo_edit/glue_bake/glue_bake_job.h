// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class EditorHost;

	/// @brief Runs only the collection step of the glue scene bake and logs what it found: the
	///        `--glue-bake-dry-run <GlueScene.json>` job.
	/// @details Opens the scene definition's world through the host, samples the glue camera poses,
	///          collects the visible render items, lights, sounds, particles, foliage and terrain pages,
	///          and logs counts and statistics. Writes nothing.
	/// @param host Opens the world.
	/// @param sceneFile The GlueScene.json, as a file system path or an asset path.
	/// @return False when the definition or world cannot be loaded or nothing visible was found.
	bool RunGlueBakeDryRun(EditorHost& host, const String& sceneFile);

	/// @brief Vertical field of view of the glue camera in degrees (the client's Camera default).
	constexpr float GlueBakeVerticalFovDegrees = 45.0f;

	/// @brief Far distance of the glue bake frusta. Shorter than the client camera's 1000 m far clip:
	///        fog hides what lies beyond, and the bake stays small enough for its load target.
	constexpr float GlueBakeFarDistance = 500.0f;
}
