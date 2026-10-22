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
#include <sstream>
#include <utility>

namespace mmo
{
	namespace
	{
		/// Serializes a mesh and writes it to an asset path or an absolute file system path.
		/// Returns the number of bytes written, or zero on failure.
		size_t WriteMeshFile(const MeshPtr& mesh, const String& path)
		{
			std::ostringstream buffer(std::ios::out | std::ios::binary);
			{
				io::StreamSink sink{ buffer };
				io::Writer writer{ sink };
				MeshSerializer serializer;
				serializer.Serialize(mesh, writer, mesh_version::Latest);
			}

			const String bytes = buffer.str();
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

		String JoinPath(const String& dir, const String& file)
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

		bool IsFinite(const Vector3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}
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

		for (const GlueRenderItem& item : items)
		{
			if (!item.world.IsFinite())
			{
				WLOG("Glue mesh bake: dropping " << item.meshPath << ", its transform is not finite");
				++counts.dropped;
				continue;
			}

			if (!item.mergeable || !item.mesh)
			{
				unmerged.push_back(GlueBakeMesh{ item.meshPath, item.world });
				++counts.unmerged;
				continue;
			}

			const Vector3 center = item.worldBounds.GetCenter();
			if (!IsFinite(center))
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
		}

		for (auto& [key, accumulator] : chunks)
		{
			bool chunkWritten = false;
			auto& parts = accumulator.GetParts();
			for (size_t partIndex = 0; partIndex < parts.size(); ++partIndex)
			{
				const String fileName = "Chunk_" + std::to_string(key.first) + "_" + std::to_string(key.second) + "_" + std::to_string(partIndex) + ".hmsh";
				const String path = JoinPath(outDir, fileName);

				const MeshPtr mesh = CreateMergedMesh(path, parts[partIndex]);
				if (!mesh)
				{
					continue;
				}

				const size_t bytes = WriteMeshFile(mesh, path);
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

		result.insert(result.end(), unmerged.begin(), unmerged.end());
		return result;
	}
}
