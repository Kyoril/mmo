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

	/// @brief Runs the collection and the chunk mesh writer of the glue scene bake and logs what it
	///        wrote: the `--glue-bake-meshes <GlueScene.json> <outDir>` debug job.
	/// @details Writes only the merged chunk meshes (no manifest), into outDir (an asset directory or an
	///          absolute file system directory, see WriteMergedGlueMeshes).
	/// @param host Opens the world.
	/// @param sceneFile The GlueScene.json, as a file system path or an asset path.
	/// @param outDir Where the chunk meshes go.
	/// @return False when loading failed, nothing was written or a file could not be written.
	bool RunGlueBakeMeshes(EditorHost& host, const String& sceneFile, const String& outDir);

	/// @brief Vertical field of view of the glue camera in degrees (the client's Camera default).
	constexpr float GlueBakeVerticalFovDegrees = 45.0f;

	/// @brief Far distance of the glue bake frusta. Shorter than the client camera's 1000 m far clip:
	///        fog hides what lies beyond, and the bake stays small enough for its load target.
	constexpr float GlueBakeFarDistance = 500.0f;
}
