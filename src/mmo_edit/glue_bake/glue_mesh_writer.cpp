// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_mesh_writer.h"

#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"
#include "scene_graph/mesh_merge.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/sub_mesh.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace mmo
{
	namespace
	{
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

		/// Approximate bytes one merged placement of a mesh adds to the written files (vertices as
		/// serialized: 15 floats and a colour; indices counted at 32 bit).
		uint64 EstimateMergedBytes(const Mesh& mesh)
		{
			uint64 bytes = 0;
			for (uint16 i = 0; i < mesh.GetSubMeshCount(); ++i)
			{
				const SubMesh& subMesh = mesh.GetSubMesh(i);
				if (subMesh.vertexData)
				{
					bytes += static_cast<uint64>(subMesh.vertexData->vertexCount) * (15 * sizeof(float) + sizeof(uint32));
				}
				if (subMesh.indexData)
				{
					bytes += static_cast<uint64>(subMesh.indexData->indexCount) * sizeof(uint32);
				}
			}
			return bytes;
		}
	}

	size_t WriteGlueBakeFile(const String& path, const String& bytes)
	{
		if (bytes.empty())
		{
			return 0;
		}

		const std::filesystem::path fsPath(path);
		if (fsPath.is_absolute())
		{
			std::error_code error;
			std::filesystem::create_directories(fsPath.parent_path(), error);
			std::ofstream file(fsPath, std::ios::out | std::ios::binary | std::ios::trunc);
			if (!file || !file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
			{
				return 0;
			}
			return bytes.size();
		}

		const std::unique_ptr<std::ostream> file = AssetRegistry::CreateNewFile(path);
		if (!file || !file->write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
		{
			return 0;
		}
		return bytes.size();
	}

	size_t WriteGlueMeshFile(const MeshPtr& mesh, const String& path)
	{
		std::ostringstream buffer(std::ios::out | std::ios::binary);
		{
			io::StreamSink sink{ buffer };
			io::Writer writer{ sink };
			MeshSerializer serializer;
			serializer.Serialize(mesh, writer, mesh_version::Latest);
		}

		return WriteGlueBakeFile(path, buffer.str());
	}

	String JoinGlueBakePath(const String& dir, const String& file)
	{
		if (dir.empty())
		{
			return file;
		}

		String result = dir;
		for (char& c : result)
		{
			if (c == '\\')
			{
				c = '/';
			}
		}
		if (result.back() != '/')
		{
			result += '/';
		}
		return result + file;
	}

	std::vector<GlueBakeMesh> WriteMergedGlueMeshes(const std::vector<GlueRenderItem>& items, const String& outDir, float chunkSize, GlueMeshWriteStats* stats)
	{
		GlueMeshWriteStats localStats;
		GlueMeshWriteStats& counts = stats ? *stats : localStats;
		counts = GlueMeshWriteStats{};

		if (!std::isfinite(chunkSize) || chunkSize <= 0.0f)
		{
			chunkSize = 48.0f;
		}

		std::vector<GlueBakeMesh> result;
		std::vector<GlueBakeMesh> unmerged;

		// Every distinct submesh is read back once, whichever chunks it is placed in
		MeshReadBackCache readBack;
		std::map<std::pair<int32, int32>, MeshMergeAccumulator> chunks;

		// Meshes placed often are drawn instanced from their original file instead of being merged,
		// so their geometry is not duplicated per placement.
		std::vector<String> placementKeys;
		placementKeys.reserve(items.size());
		for (const GlueRenderItem& item : items)
		{
			String key = item.meshPath;
			for (const String& material : item.materialOverrides)
			{
				key += '\n';
				key += material;
			}
			placementKeys.push_back(std::move(key));
		}
		const std::vector<bool> repeated = FindRepeatedPlacements(placementKeys, MinInstancedPlacements);
		std::set<String> instancedMeshes;

		for (size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
		{
			const GlueRenderItem& item = items[itemIndex];
			if (!item.world.IsFinite())
			{
				WLOG("Glue mesh bake: dropping " << item.meshPath << ", its transform is not finite");
				++counts.dropped;
				continue;
			}

			if (!item.mesh)
			{
				// Referencing a mesh that did not load could point at a missing file
				WLOG("Glue mesh bake: dropping " << item.meshPath << ", its mesh did not load");
				++counts.dropped;
				continue;
			}

			if (!item.mergeable)
			{
				unmerged.push_back(GlueBakeMesh{ item.meshPath, item.world, item.materialOverrides });
				++counts.unmerged;
				continue;
			}

			if (repeated[itemIndex])
			{
				unmerged.push_back(GlueBakeMesh{ item.meshPath, item.world, item.materialOverrides });
				++counts.instanced;
				counts.instancedTriangles += CountTriangles(*item.mesh);
				instancedMeshes.insert(placementKeys[itemIndex]);
				continue;
			}

			const Vector3 center = item.worldBounds.GetCenter();
			if (!IsFiniteVector(center))
			{
				WLOG("Glue mesh bake: dropping " << item.meshPath << ", its bounds are not finite");
				++counts.dropped;
				continue;
			}

			const auto key = std::make_pair(
				static_cast<int32>(std::floor(center.x / chunkSize)),
				static_cast<int32>(std::floor(center.z / chunkSize)));
			auto it = chunks.find(key);
			if (it == chunks.end())
			{
				it = chunks.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(readBack)).first;
			}

			if (!it->second.Add(*item.mesh, item.world, item.materialOverrides))
			{
				WLOG("Glue mesh bake: " << item.meshPath << " was merged only in part (a submesh could not be read back or was not finite)");
				++counts.incomplete;
			}

			++counts.merged;
			GlueMeshWriteStats::MeshUse& use = counts.mergedByMesh[item.meshPath];
			++use.placements;
			use.bytes += EstimateMergedBytes(*item.mesh);
		}

		for (auto& [key, accumulator] : chunks)
		{
			bool chunkWritten = false;
			auto& parts = accumulator.GetParts();
			for (size_t partIndex = 0; partIndex < parts.size(); ++partIndex)
			{
				const String fileName = "Chunk_" + std::to_string(key.first) + "_" + std::to_string(key.second) + "_" + std::to_string(partIndex) + ".hmsh";
				const String path = JoinGlueBakePath(outDir, fileName);

				const MeshPtr mesh = CreateMergedMesh(path, parts[partIndex]);
				if (!mesh)
				{
					continue;
				}

				const size_t bytes = WriteGlueMeshFile(mesh, path);
				if (bytes == 0)
				{
					ELOG("Glue mesh bake: failed to write " << path);
					++counts.failedFiles;
					continue;
				}

				for (uint16 i = 0; i < mesh->GetSubMeshCount(); ++i)
				{
					const SubMesh& subMesh = mesh->GetSubMesh(i);
					counts.vertices += subMesh.vertexData->vertexCount;
					counts.triangles += subMesh.indexData->indexCount / 3;
				}
				counts.subMeshes += mesh->GetSubMeshCount();
				counts.bytes += bytes;
				++counts.files;
				chunkWritten = true;

				result.push_back(GlueBakeMesh{ path, Matrix4::Identity });
			}

			// The merged copies are written; free them before the next chunk is built
			parts.clear();
			if (chunkWritten)
			{
				++counts.chunks;
			}
		}

		counts.instancedMeshes = static_cast<uint32>(instancedMeshes.size());
		result.insert(result.end(), unmerged.begin(), unmerged.end());
		return result;
	}
}
