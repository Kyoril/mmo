// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_scene_baker.h"

#include "glue_bake_job.h"
#include "glue_mesh_writer.h"
#include "glue_scene_collector.h"
#include "glue_terrain_baker.h"

#include "editor_host.h"
#include "editors/world_editor/world_editor_instance.h"

#include "assets/asset_registry.h"
#include "game_common/glue_bake.h"
#include "game_common/glue_scene_definition.h"
#include "graphics/material.h"
#include "log/default_log_levels.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_merge.h"
#include "terrain/page.h"

#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>

namespace mmo
{
	namespace
	{
		/// Replaces backslashes with forward slashes.
		String ToForwardSlashes(String path)
		{
			for (char& c : path)
			{
				if (c == '\\')
				{
					c = '/';
				}
			}
			return path;
		}

		/// The glue scene definition's bytes and where they came from.
		struct GlueSceneSource
		{
			String bytes;

			/// The asset path the bytes were read from; empty when they came from the file system.
			String assetPath;
		};

		/// Maps a path to the asset path it names: the path itself when the asset registry knows it, or the part
		/// after "data/client/" of a file system path into the client data directory.
		std::optional<String> ResolveSceneAssetPath(const String& path)
		{
			const String normalized = ToForwardSlashes(path);
			if (AssetRegistry::HasFile(normalized))
			{
				return normalized;
			}

			static const String clientDir = "data/client/";
			const size_t index = normalized.rfind(clientDir);
			if (index != String::npos)
			{
				const String assetPath = normalized.substr(index + clientDir.size());
				if (AssetRegistry::HasFile(assetPath))
				{
					return assetPath;
				}
			}

			return std::nullopt;
		}

		/// Reads the glue scene definition through the asset registry (which gives the bytes the client hashes),
		/// or else from the file system.
		std::optional<GlueSceneSource> ReadGlueSceneSource(const String& path)
		{
			GlueSceneSource source;

			if (const std::optional<String> assetPath = ResolveSceneAssetPath(path))
			{
				const std::unique_ptr<std::istream> asset = AssetRegistry::OpenFile(*assetPath);
				if (asset)
				{
					source.assetPath = *assetPath;
					source.bytes.assign(std::istreambuf_iterator<char>(*asset), std::istreambuf_iterator<char>());
					return source;
				}
			}

			std::ifstream file(path, std::ios::in | std::ios::binary);
			if (!file)
			{
				return std::nullopt;
			}

			WLOG("Glue scene bake: " << path << " is not an asset of the client data; the scene hash is taken from the file system bytes");
			source.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			return source;
		}

		/// Triangles of all submeshes of a mesh.
		uint64 CountTriangles(const Mesh& mesh)
		{
			uint64 triangles = 0;
			for (uint16 i = 0; i < mesh.GetSubMeshCount(); ++i)
			{
				const SubMesh& subMesh = mesh.GetSubMesh(i);
				if (subMesh.indexData)
				{
					triangles += subMesh.indexData->indexCount / 3;
				}
			}
			return triangles;
		}

		/// Whether a manifest path lies in the bake's output directory, and so belongs to the bake.
		bool IsOwnedBakeFile(const String& path)
		{
			static const String prefix = String(GlueBakeOutputDirectory) + "/";
			return ToForwardSlashes(path).rfind(prefix, 0) == 0;
		}

		/// Removes the files of the previous bake: the files its manifest lists inside the output directory, the
		/// texture next to each listed material instance (the far albedo), and the manifest itself. Nothing else
		/// in the directory is touched.
		/// @return The number of files removed.
		uint32 ClearPreviousGlueBake()
		{
			uint32 removed = 0;

			String json;
			{
				// Closed again before anything is removed: removing an open file fails on Windows
				const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(GlueBakeManifestPath);
				if (!file)
				{
					return removed;
				}

				json.assign(std::istreambuf_iterator<char>(*file), std::istreambuf_iterator<char>());
			}

			const std::optional<GlueBakeManifest> manifest = ParseGlueBakeManifest(json);
			if (!manifest)
			{
				WLOG("Glue scene bake: the previous manifest " << GlueBakeManifestPath << " is malformed, only it is removed");
			}
			else
			{
				std::set<String> owned;
				const auto addOwned = [&owned](const String& path)
				{
					if (!path.empty() && IsOwnedBakeFile(path))
					{
						owned.insert(ToForwardSlashes(path));
					}
				};

				for (const GlueBakeMesh& mesh : manifest->meshes)
				{
					addOwned(mesh.mesh);
				}
				for (const GlueBakeMesh& mesh : manifest->water)
				{
					addOwned(mesh.mesh);
				}
				for (const GlueBakeTerrainTile& tile : manifest->terrainTiles)
				{
					addOwned(tile.mesh);
					addOwned(tile.material);
					addOwned(tile.splatting);

					// A far tile's material instance carries its albedo texture of the same name
					if (tile.material.ends_with(".hmi"))
					{
						addOwned(tile.material.substr(0, tile.material.size() - 4) + ".htex");
					}
				}
				for (const GlueBakeFoliage& foliage : manifest->foliage)
				{
					addOwned(foliage.mesh);
				}
				for (const GlueBakeParticles& particles : manifest->particles)
				{
					addOwned(particles.file);
				}

				for (const String& path : owned)
				{
					if (AssetRegistry::RemoveFile(path))
					{
						++removed;
					}
				}
			}

			// Removed last, so a bake interrupted while clearing still finds what is left to clear
			if (AssetRegistry::RemoveFile(GlueBakeManifestPath))
			{
				++removed;
			}

			return removed;
		}

		/// Collects the summary lines and logs each.
		class SummaryWriter final
		{
		public:
			template <typename T>
			SummaryWriter& operator<<(const T& value)
			{
				m_line << value;
				return *this;
			}

			/// Ends the current line.
			void EndLine()
			{
				const String line = m_line.str();
				ILOG(line);
				m_text += line;
				m_text += '\n';
				m_line.str(String());
				m_line.clear();
			}

			[[nodiscard]] const String& GetText() const
			{
				return m_text;
			}

		private:
			std::ostringstream m_line;
			String m_text;
		};

		/// Megabytes with two decimals.
		String FormatMegabytes(const uint64 bytes)
		{
			std::ostringstream stream;
			stream << std::fixed << std::setprecision(2) << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MB";
			return stream.str();
		}

		/// Fails the bake with a message.
		bool Fail(String& outSummary, const String& message)
		{
			ELOG("Glue scene bake: " << message);
			outSummary = message;
			return false;
		}
	}

	bool BakeGlueScene(WorldEditorInstance& world, const String& glueSceneJsonPath, String& outSummary)
	{
		outSummary.clear();

		const std::optional<GlueSceneSource> source = ReadGlueSceneSource(glueSceneJsonPath);
		if (!source)
		{
			return Fail(outSummary, "cannot read " + glueSceneJsonPath);
		}

		const std::optional<GlueSceneDefinition> definition = ParseGlueSceneDefinition(source->bytes);
		if (!definition)
		{
			return Fail(outSummary, glueSceneJsonPath + " is not a valid glue scene definition");
		}

		const String expectedWorld = ToForwardSlashes(definition->world + ".hwld");
		const String openWorld = ToForwardSlashes(world.GetWorldPath().generic_string());
		if (openWorld != expectedWorld)
		{
			return Fail(outSummary, "the glue scene is set in " + expectedWorld + ", but the open world is " + openWorld);
		}

		const std::vector<GlueCameraPose> poses = SampleGlueCameraPoses(*definition, 48);
		if (poses.empty())
		{
			return Fail(outSummary, "the glue scene has no flight path, so there is no camera pose to bake for");
		}

		const GlueBakeFrustumSet frusta = BuildGlueBakeFrusta(poses, GlueBakeVerticalFovDegrees, GlueBakeFarDistance);
		ILOG("Glue scene bake: " << poses.size() << " camera poses, " << frusta.frusta.size() << " frusta (vertical FOV "
			<< GlueBakeVerticalFovDegrees << ", far " << GlueBakeFarDistance << ")");

		const uint32 removed = ClearPreviousGlueBake();
		ILOG("Glue scene bake: removed " << removed << " files of the previous bake");

		const GlueCollection collection = CollectGlueScene(world, *definition, frusta);
		if (collection.items.empty() || collection.terrainPages.empty())
		{
			return Fail(outSummary, "nothing visible was collected");
		}

		GlueMeshWriteStats meshStats;
		std::vector<GlueBakeMesh> meshes = WriteMergedGlueMeshes(collection.items, GlueBakeOutputDirectory, 48.0f, &meshStats);

		GlueTerrainBakeStats terrainStats;
		std::vector<GlueBakeMesh> water;
		std::vector<GlueBakeTerrainTile> tiles = BakeGlueTerrain(world, collection.terrainPages, frusta, poses, GlueBakeOutputDirectory,
			GlueBakeNearDistance, &terrainStats, &water);

		if (meshStats.failedFiles > 0 || terrainStats.failedFiles > 0)
		{
			return Fail(outSummary, std::to_string(meshStats.failedFiles + terrainStats.failedFiles) + " bake files could not be written");
		}
		if (meshes.empty() || tiles.empty())
		{
			return Fail(outSummary, "the bake produced no meshes or no terrain");
		}

		// Statistics the writers do not keep: unmerged triangles and the distinct materials
		uint64 unmergedTriangles = 0;
		std::set<String> materials;
		for (const GlueRenderItem& item : collection.items)
		{
			if (!item.mesh)
			{
				continue;
			}

			if (!item.mergeable)
			{
				unmergedTriangles += CountTriangles(*item.mesh);
			}

			for (uint16 i = 0; i < item.mesh->GetSubMeshCount(); ++i)
			{
				if (i < item.materialOverrides.size() && !item.materialOverrides[i].empty())
				{
					materials.insert(item.materialOverrides[i]);
				}
				else if (const MaterialPtr& material = item.mesh->GetSubMesh(i).GetMaterial())
				{
					materials.insert(String(material->GetName()));
				}
			}
		}
		for (const GlueBakeTerrainTile& tile : tiles)
		{
			materials.insert(tile.material);
		}

		GlueBakeManifest manifest;
		manifest.sceneHash = HashGlueBakeBytes(source->bytes.data(), source->bytes.size());
		manifest.inputs = collection.inputs;
		manifest.meshes = std::move(meshes);
		manifest.terrainTiles = std::move(tiles);
		manifest.water = std::move(water);
		manifest.foliage = collection.foliage;
		manifest.lights = collection.lights;
		manifest.sounds = collection.sounds;
		manifest.particles = collection.particles;

		// Bounds of everything the bake draws: the collected meshes, foliage and the visible terrain pages
		manifest.bounds.SetNull();
		for (const GlueRenderItem& item : collection.items)
		{
			manifest.bounds.Combine(item.worldBounds);
		}
		for (const GlueBakeFoliage& foliage : manifest.foliage)
		{
			manifest.bounds.Combine(foliage.position);
		}
		for (const PagePosition& page : collection.terrainPages)
		{
			if (const terrain::Page* terrainPage = world.GetPreparedTerrainPage(page))
			{
				manifest.bounds.Combine(terrainPage->GetBoundingBox());
			}
		}

		const String manifestJson = WriteGlueBakeManifest(manifest);
		const size_t manifestBytes = WriteGlueBakeFile(GlueBakeManifestPath, manifestJson);
		if (manifestBytes == 0)
		{
			return Fail(outSummary, String("cannot write ") + GlueBakeManifestPath);
		}

		const uint64 totalBytes = meshStats.bytes + terrainStats.fileBytes + manifestBytes;
		const uint64 totalTriangles = meshStats.triangles + meshStats.instancedTriangles + unmergedTriangles
			+ terrainStats.nearTriangles + terrainStats.farTriangles + terrainStats.waterTriangles;

		SummaryWriter summary;
		summary << "Glue scene bake summary (" << GlueBakeOutputDirectory << ", scene hash " << manifest.sceneHash << "):"; summary.EndLine();
		summary << "  triangles:         " << totalTriangles << " (merged " << meshStats.triangles << ", instanced " << meshStats.instancedTriangles
			<< ", unmerged " << unmergedTriangles << ", terrain " << terrainStats.nearTriangles + terrainStats.farTriangles
			<< ", water " << terrainStats.waterTriangles << ")"; summary.EndLine();
		summary << "  meshes:            " << manifest.meshes.size() << " entries (" << meshStats.files << " chunk files in " << meshStats.chunks
			<< " chunks, " << meshStats.instanced << " instanced placements of " << meshStats.instancedMeshes << " meshes, "
			<< meshStats.unmerged << " unmerged)"; summary.EndLine();
		summary << "  terrain:           " << manifest.terrainTiles.size() << " meshes (" << terrainStats.nearTiles << " near tiles, "
			<< terrainStats.farTiles << " far tiles in " << terrainStats.farPages << " pages, " << terrainStats.emptyTiles << " empty)"; summary.EndLine();
		summary << "  water:             " << manifest.water.size() << " meshes (" << terrainStats.waterQuads << " quads)"; summary.EndLine();
		summary << "  materials:         " << materials.size(); summary.EndLine();
		summary << "  texture memory:    " << FormatMegabytes(terrainStats.textureBytes) << " baked (splatting maps and far albedo)"; summary.EndLine();
		summary << "  skipped interior:  " << collection.skippedInterior << " groups (" << collection.skippedInteriorMeshes << " meshes, "
			<< collection.skippedInteriorChildren << " child world models inside)"; summary.EndLine();
		summary << "  skipped small:     " << collection.skippedSmall; summary.EndLine();
		summary << "  skipped outside:   " << collection.skippedOutside; summary.EndLine();
		summary << "  unmerged meshes:   " << meshStats.unmerged << " (" << collection.unmergeable << " unmergeable items)"; summary.EndLine();
		summary << "  dropped:           " << meshStats.dropped << " (" << collection.missingMeshes << " missing meshes, "
			<< meshStats.incomplete << " merged in part)"; summary.EndLine();
		summary << "  foliage:           " << manifest.foliage.size(); summary.EndLine();
		summary << "  lights:            " << manifest.lights.size(); summary.EndLine();
		summary << "  sounds:            " << manifest.sounds.size() << " (" << collection.inaudibleSounds << " out of range)"; summary.EndLine();
		summary << "  particles:         " << manifest.particles.size(); summary.EndLine();
		summary << "  inputs:            " << manifest.inputs.size() << " files"; summary.EndLine();
		summary << "  output:            " << meshStats.files + terrainStats.files + 1 << " files, " << FormatMegabytes(totalBytes)
			<< " (meshes " << FormatMegabytes(meshStats.bytes) << ", terrain and water " << FormatMegabytes(terrainStats.fileBytes)
			<< ", manifest " << FormatMegabytes(manifestBytes) << ")"; summary.EndLine();

		outSummary = summary.GetText();
		return true;
	}

	bool BakeGlueScene(EditorHost& host, const String& glueSceneJsonPath, String& outSummary)
	{
		const std::optional<GlueSceneSource> source = ReadGlueSceneSource(glueSceneJsonPath);
		if (!source)
		{
			return Fail(outSummary, "cannot read " + glueSceneJsonPath);
		}

		const std::optional<GlueSceneDefinition> definition = ParseGlueSceneDefinition(source->bytes);
		if (!definition)
		{
			return Fail(outSummary, glueSceneJsonPath + " is not a valid glue scene definition");
		}

		const String worldAsset = definition->world + ".hwld";
		if (!host.OpenAsset(worldAsset) || !WorldEditorInstance::GetLastCreated())
		{
			return Fail(outSummary, "failed to open " + worldAsset);
		}

		return BakeGlueScene(*WorldEditorInstance::GetLastCreated(), glueSceneJsonPath, outSummary);
	}
}
