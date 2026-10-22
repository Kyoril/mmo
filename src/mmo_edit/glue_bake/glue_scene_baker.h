// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class EditorHost;
	class WorldEditorInstance;

	/// @brief Asset directory the glue scene bake writes into (asset-relative, so the files can reference each other).
	constexpr const char* GlueBakeOutputDirectory = "Interface/GlueUI/GlueBake";

	/// @brief Asset path of the bake manifest.
	constexpr const char* GlueBakeManifestPath = "Interface/GlueUI/GlueBake/GlueBake.json";

	/// @brief Asset path of the glue scene definition the client uses.
	constexpr const char* GlueSceneDefinitionPath = "Interface/GlueUI/GlueScene.json";

	/// @brief Bakes the glue scene from the world that is open in the world editor.
	/// @details Reads the scene definition (preferably through the asset registry, whose bytes give the manifest's
	///          sceneHash), removes the files of the previous bake (only those its manifest lists inside
	///          GlueBakeOutputDirectory, the far albedo textures of its listed material instances, and the manifest
	///          itself), collects what the glue cameras see, writes the merged meshes, the terrain and water, and
	///          finally the manifest GlueBakeManifestPath. Logs a summary and returns it in outSummary.
	/// @param world The world editor instance; must hold the world the definition names.
	/// @param glueSceneJsonPath The GlueScene.json, as an asset path or a file system path (a file system path
	///        inside the asset registry's data/client directory is mapped to its asset path).
	/// @param outSummary Receives the summary lines (or the reason of a failure).
	/// @return False when the definition cannot be read, the world does not match, nothing visible was found or
	///         a file could not be written. No manifest is written then.
	bool BakeGlueScene(WorldEditorInstance& world, const String& glueSceneJsonPath, String& outSummary);

	/// @brief Logs a reminder to rebake when the given world is the one the glue scene definition names
	///        (the bake does not follow edits on its own). Does nothing for other worlds or when the
	///        definition cannot be read.
	/// @param world The world that was just saved.
	void RemindGlueBakeMayBeStale(const WorldEditorInstance& world);

	/// @brief Opens the world the glue scene definition names through the host and bakes it (the
	///        `--bake-glue-scene <GlueScene.json>` job), see BakeGlueScene(WorldEditorInstance&, ...).
	bool BakeGlueScene(EditorHost& host, const String& glueSceneJsonPath, String& outSummary);
}
