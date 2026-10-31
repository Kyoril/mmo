// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_graph_hash.h"

#include <algorithm>
#include <map>
#include <memory>

#include "assets/asset_registry.h"
#include "base/chunk_reader.h"
#include "base/chunk_writer.h"
#include "base/utilities.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"
#include "material_graph/material_graph.h"
#include "material_graph/material_node.h"

namespace mmo
{
	namespace
	{
		/// @brief Function paths already handled, compared case-insensitively like the function manager does.
		using VisitedFunctions = std::map<String, SHA1Hash, StrCaseIComp>;

		/// @brief Reads a material function file: its stored hash and, if wanted, its graph.
		/// @param path Asset path of the function.
		/// @param hash Receives the stored hash.
		/// @param graph If not null, receives the function's graph.
		/// @return false if the file is missing or unreadable. Without a stored hash it is computed from the graph.
		bool ReadFunctionFile(const String& path, SHA1Hash& hash, MaterialGraph* graph)
		{
			const auto file = AssetRegistry::OpenFile(path);
			if (!file)
			{
				WLOG("Material function file " << path << " is missing, can't hash it");
				return false;
			}

			io::StreamSource source(*file);
			io::Reader reader(source);

			bool hasHash = false;
			ExecutableMaterialGraphLoadContext loadContext;

			ChunkReader chunkReader;
			chunkReader.SetIgnoreUnhandledChunks(true);
			chunkReader.AddChunkHandler(*ChunkMagic({ 'F', 'H', 'S', 'H' }), false, [&hash, &hasHash](io::Reader& chunkStream, uint32, uint32) -> bool
			{
				hasHash = static_cast<bool>(chunkStream >> io::read_range(hash));
				return hasHash;
			});

			if (graph)
			{
				// Custom nodes only available in material functions
				const auto registry = graph->GetNodeRegistry();
				registry->RegisterNodeType(MaterialFunctionOutputNode::GetStaticTypeInfo());
				registry->RegisterNodeType(MaterialFunctionInputNode::GetStaticTypeInfo());

				chunkReader.AddChunkHandler(*ChunkMagic({ 'G', 'R', 'P', 'H' }), false, [graph, &loadContext](io::Reader& chunkStream, uint32, uint32) -> bool
				{
					return graph->Deserialize(chunkStream, loadContext);
				});
			}

			if (!chunkReader.Read(reader) || (graph && !loadContext.PerformAfterLoadActions()))
			{
				WLOG("Unable to read material function file " << path << ", can't hash it");
				return false;
			}

			if (!hasHash)
			{
				// Legacy file saved before hashes were stored: hash what saving would have recorded
				if (!graph)
				{
					return false;
				}

				DLOG("Material function file " << path << " has no stored hash, computing it");
				hash = ComputeGraphHash(*graph);
			}

			return true;
		}

		bool CollectRecursive(const MaterialGraph& graph, VisitedFunctions& visited)
		{
			for (const GraphNode* node : graph.GetNodes())
			{
				const auto* functionNode = dynamic_cast<const MaterialFunctionNode*>(node);
				if (!functionNode)
				{
					continue;
				}

				const String path(functionNode->GetMaterialFunction());
				if (path.empty() || visited.contains(path))
				{
					continue;
				}

				SHA1Hash hash{};
				MaterialGraph functionGraph;
				if (!ReadFunctionFile(path, hash, &functionGraph))
				{
					return false;
				}

				// Registered before recursing, so a cycle ends at the second visit
				visited.emplace(path, hash);
				if (!CollectRecursive(functionGraph, visited))
				{
					return false;
				}
			}

			return true;
		}
	}

	SHA1Hash ComputeGraphHash(const MaterialGraph& graph)
	{
		std::vector<char> buffer;
		{
			io::VectorSink sink{ buffer };
			io::Writer writer{ sink };
			graph.SerializeLayoutFree(writer);
		}

		return sha1(buffer.data(), buffer.size());
	}

	bool CollectFunctionHashes(const MaterialGraph& graph, std::vector<FunctionHash>& out)
	{
		VisitedFunctions visited;
		if (!CollectRecursive(graph, visited))
		{
			return false;
		}

		std::vector<FunctionHash> result;
		result.reserve(visited.size());
		for (const auto& [path, hash] : visited)
		{
			result.push_back({ path, hash });
		}

		std::sort(result.begin(), result.end(), [](const FunctionHash& a, const FunctionHash& b)
		{
			return a.path < b.path;
		});

		out = std::move(result);
		return true;
	}

	bool IsShaderBuildCurrent(const ShaderBuildInfo& info, const std::string_view platform, const SHA1Hash& graphHash, const std::vector<FunctionHash>& currentFunctions)
	{
		if (info.graphHash != graphHash
			|| info.generatorVersion != material_compiler_version::Generator
			|| info.backendVersion != material_compiler_version::GetBackendVersion(platform)
			|| info.functions.size() != currentFunctions.size())
		{
			return false;
		}

		for (size_t i = 0; i < currentFunctions.size(); ++i)
		{
			if (info.functions[i].path != currentFunctions[i].path || info.functions[i].hash != currentFunctions[i].hash)
			{
				return false;
			}
		}

		return true;
	}
}
