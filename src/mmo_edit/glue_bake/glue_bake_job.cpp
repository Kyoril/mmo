// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_bake_job.h"

#include "glue_mesh_writer.h"
#include "glue_scene_collector.h"
#include "glue_terrain_baker.h"
#include "glue_terrain_compare.h"

#include "editor_host.h"
#include "editors/world_editor/world_editor_instance.h"

#include "assets/asset_registry.h"
#include "game_common/glue_bake.h"
#include "game_common/glue_scene_definition.h"
#include "scene_graph/mesh_merge.h"
#include "log/default_log_levels.h"
#include "terrain/constants.h"
#include "terrain/page.h"

#include <algorithm>
#include <bit>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>

namespace mmo
{
	namespace
	{
		/// Reads a file from the file system, or else from the asset registry.
		std::optional<String> ReadSceneFile(const String& path)
		{
			std::ifstream file(path, std::ios::in | std::ios::binary);
			if (file)
			{
				return String{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
			}

			const std::unique_ptr<std::istream> asset = AssetRegistry::OpenFile(path);
			if (asset)
			{
				return String{ std::istreambuf_iterator<char>(*asset), std::istreambuf_iterator<char>() };
			}

			return std::nullopt;
		}

		/// Reads the scene definition, opens its world and collects what the glue cameras see.
		/// @param label Prefix of the log lines.
		bool LoadAndCollect(EditorHost& host, const String& sceneFile, const char* label, WorldEditorInstance*& world, GlueCollection& collection,
			std::vector<GlueCameraPose>* outPoses = nullptr, GlueBakeFrustumSet* outFrusta = nullptr)
		{
			const std::optional<String> json = ReadSceneFile(sceneFile);
			if (!json)
			{
				ELOG(label << ": cannot read " << sceneFile);
				return false;
			}

			const std::optional<GlueSceneDefinition> definition = ParseGlueSceneDefinition(*json);
			if (!definition)
			{
				ELOG(label << ": " << sceneFile << " is not a valid glue scene definition");
				return false;
			}

			const std::vector<GlueCameraPose> poses = SampleGlueCameraPoses(*definition, 48);
			if (poses.empty())
			{
				ELOG(label << ": the glue scene has no flight path, so there is no camera pose to bake for");
				return false;
			}

			const GlueBakeFrustumSet frusta = BuildGlueBakeFrusta(poses, GlueBakeVerticalFovDegrees, GlueBakeFarDistance);
			ILOG(label << ": " << poses.size() << " camera poses, " << frusta.frusta.size() << " frusta (vertical FOV "
				<< GlueBakeVerticalFovDegrees << ", far " << GlueBakeFarDistance << ")");

			const String worldAsset = definition->world + ".hwld";
			if (!host.OpenAsset(worldAsset) || !WorldEditorInstance::GetLastCreated())
			{
				ELOG(label << ": failed to open " << worldAsset);
				return false;
			}

			world = WorldEditorInstance::GetLastCreated();
			collection = CollectGlueScene(*world, *definition, frusta);
			if (outPoses)
			{
				*outPoses = poses;
			}
			if (outFrusta)
			{
				*outFrusta = frusta;
			}
			return true;
		}
	}

	bool RunGlueBakeDryRun(EditorHost& host, const String& sceneFile)
	{
		WorldEditorInstance* worldInstance = nullptr;
		GlueCollection collection;
		if (!LoadAndCollect(host, sceneFile, "Glue bake dry run", worldInstance, collection))
		{
			return false;
		}

		WorldEditorInstance& world = *worldInstance;

		// Distinct meshes, so the log shows what made it in by path
		std::map<String, uint32> meshCounts;
		for (const GlueRenderItem& item : collection.items)
		{
			++meshCounts[item.meshPath];
		}

		uint64 waterQuads = 0;
		for (const PagePosition& page : collection.terrainPages)
		{
			ILOG("  terrain page " << page.x() << "," << page.y());

			const terrain::Page* terrainPage = world.GetPreparedTerrainPage(page);
			if (!terrainPage)
			{
				continue;
			}

			for (uint32 tileZ = 0; tileZ < terrain::constants::TilesPerPage; ++tileZ)
			{
				for (uint32 tileX = 0; tileX < terrain::constants::TilesPerPage; ++tileX)
				{
					waterQuads += static_cast<uint64>(std::popcount(terrainPage->GetWaterQuadMask(tileX, tileZ)));
				}
			}
		}

		for (const GlueBakeInputFile& input : collection.inputs)
		{
			if (input.path.ends_with(".hwmo") || input.path.ends_with(".hkit"))
			{
				ILOG("  input " << input.path << " " << input.hash);
			}
		}

		for (const auto& [path, count] : meshCounts)
		{
			ILOG("  mesh " << path << " x" << count);
		}

		ILOG("Glue bake dry run summary:");
		ILOG("  items:                 " << collection.items.size() << " (" << meshCounts.size() << " distinct meshes)");
		ILOG("  unmergeable:           " << collection.unmergeable);
		ILOG("  foliage:               " << collection.foliage.size());
		ILOG("  lights:                " << collection.lights.size());
		ILOG("  sounds:                " << collection.sounds.size() << " (" << collection.inaudibleSounds << " out of range)");
		ILOG("  particles:             " << collection.particles.size());
		ILOG("  terrain pages:         " << collection.terrainPages.size() << " (" << waterQuads << " water quads)");
		ILOG("  entity pages scanned:  " << collection.entityPages.size());
		ILOG("  inputs:                " << collection.inputs.size());
		ILOG("  skippedInterior:       " << collection.skippedInterior << " groups (" << collection.skippedInteriorMeshes << " meshes inside them)");
		ILOG("  includedInterior:      " << collection.includedInterior << " groups (facade meshes kept, contents skipped)");
		ILOG("  skippedInteriorChildren: " << collection.skippedInteriorChildren);
		ILOG("  skippedSmall:          " << collection.skippedSmall);
		ILOG("  skippedOutside:        " << collection.skippedOutside);
		ILOG("  missingMeshes:         " << collection.missingMeshes);

		if (collection.items.empty() || collection.terrainPages.empty())
		{
			ELOG("Glue bake dry run: nothing visible was collected");
			return false;
		}

		return true;
	}

	bool RunGlueBakeMeshes(EditorHost& host, const String& sceneFile, const String& outDir)
	{
		WorldEditorInstance* world = nullptr;
		GlueCollection collection;
		if (!LoadAndCollect(host, sceneFile, "Glue mesh bake", world, collection))
		{
			return false;
		}

		GlueMeshWriteStats stats;
		const std::vector<GlueBakeMesh> meshes = WriteMergedGlueMeshes(collection.items, outDir, 48.0f, &stats);

		// The merged meshes that cost the most bytes, to see what to instance or drop next
		std::vector<std::pair<String, GlueMeshWriteStats::MeshUse>> heaviest(stats.mergedByMesh.begin(), stats.mergedByMesh.end());
		std::sort(heaviest.begin(), heaviest.end(), [](const auto& a, const auto& b) { return a.second.bytes > b.second.bytes; });
		for (size_t i = 0; i < heaviest.size() && i < 10; ++i)
		{
			ILOG("  merged mesh " << heaviest[i].first << " x" << heaviest[i].second.placements << " ~" << heaviest[i].second.bytes << " bytes");
		}

		ILOG("Glue mesh bake summary (" << outDir << "):");
		ILOG("  items:               " << collection.items.size());
		ILOG("  merged items:        " << stats.merged);
		ILOG("  instanced items:     " << stats.instanced << " (" << stats.instancedMeshes << " distinct meshes, placed >= " << MinInstancedPlacements << " times)");
		ILOG("  unmergeable items:   " << stats.unmerged);
		ILOG("  chunks:              " << stats.chunks << " (" << stats.files << " files)");
		ILOG("  submeshes:           " << stats.subMeshes);
		ILOG("  merged vertices:     " << stats.vertices);
		ILOG("  merged triangles:    " << stats.triangles);
		ILOG("  instanced triangles: " << stats.instancedTriangles);
		ILOG("  dropped:             " << stats.dropped);
		ILOG("  incomplete:          " << stats.incomplete);
		ILOG("  failed files:        " << stats.failedFiles);
		ILOG("  written bytes:       " << stats.bytes);
		ILOG("  manifest meshes:     " << meshes.size());

		return stats.failedFiles == 0 && !meshes.empty();
	}

	bool RunGlueBakeTerrain(EditorHost& host, const String& sceneFile, const String& outDir, const String& compareDir)
	{
		WorldEditorInstance* world = nullptr;
		GlueCollection collection;
		std::vector<GlueCameraPose> poses;
		GlueBakeFrustumSet frusta;
		if (!LoadAndCollect(host, sceneFile, "Glue terrain bake", world, collection, &poses, &frusta))
		{
			return false;
		}

		GlueTerrainBakeStats stats;
		std::vector<GlueBakeMesh> water;
		const std::vector<GlueBakeTerrainTile> tiles = BakeGlueTerrain(*world, collection.terrainPages, frusta, poses, outDir, GlueBakeNearDistance, &stats, &water);

		ILOG("Glue terrain bake summary (" << outDir << "):");
		ILOG("  candidate pages:   " << collection.terrainPages.size() << " (" << stats.pages << " with visible tiles)");
		ILOG("  near tiles:        " << stats.nearTiles << " (" << stats.nearTriangles << " triangles)");
		ILOG("  far tiles:         " << stats.farTiles << " in " << stats.farPages << " page meshes (" << stats.farTriangles << " triangles)");
		ILOG("  empty tiles:       " << stats.emptyTiles);
		ILOG("  water:             " << stats.waterQuads << " quads in " << stats.waterMeshes << " meshes (" << stats.waterTriangles << " triangles)");
		ILOG("  vertices:          " << stats.vertices);
		ILOG("  texture memory:    " << stats.textureBytes << " bytes");
		ILOG("  files:             " << stats.files << " (" << stats.fileBytes << " bytes)");
		ILOG("  failed files:      " << stats.failedFiles);
		ILOG("  manifest entries:  " << tiles.size());

		bool success = stats.failedFiles == 0 && !tiles.empty();
		if (success && !compareDir.empty() && !poses.empty())
		{
			// The flight's start, middle and end, and a look down onto the plaza's near tiles.
			std::vector<GlueTerrainCompareView> views;
			views.push_back({ "flight_start", poses.front() });
			views.push_back({ "flight_middle", poses[std::min<size_t>(24, poses.size() - 1)] });
			views.push_back({ "flight_end", poses[std::min<size_t>(47, poses.size() - 1)] });

			GlueCameraPose closeUp;
			closeUp.position = poses[std::min<size_t>(47, poses.size() - 1)].position + Vector3(0.0f, 4.0f, 0.0f);
			closeUp.lookAt = closeUp.position + Vector3(0.0f, -6.0f, 6.0f);
			views.push_back({ "near_closeup", closeUp });

			success = CompareGlueTerrainBake(*world, collection.terrainPages, tiles, water, views, compareDir);
		}

		return success;
	}
}
