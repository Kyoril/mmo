// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string_view>
#include <vector>

#include "base/sha1.h"
#include "graphics/shader_build_info.h"

namespace mmo
{
	class MaterialGraph;

	/// @brief Computes the content hash of a material graph: SHA1 over the graph serialized without node
	///        positions and sizes, with node and pin ids in their stored order. Moving nodes around does
	///        not change it.
	/// @param graph The graph to hash.
	/// @return The hash.
	SHA1Hash ComputeGraphHash(const MaterialGraph& graph);

	/// @brief Collects the stored content hashes of all material functions a graph uses, recursing through
	///        material functions used by material functions.
	/// @param graph The graph to scan.
	/// @param out Receives the functions sorted by path, each listed once. Replaced on success only.
	/// @return false (with a warning) if a function file is missing or has no stored hash.
	bool CollectFunctionHashes(const MaterialGraph& graph, std::vector<FunctionHash>& out);

	/// @brief Decides whether stored shader build info still matches the current content.
	/// @param info The recorded build info.
	/// @param platform Shader platform id, selects the backend version to compare against.
	/// @param graphHash The current graph hash.
	/// @param currentFunctions The current function hashes, sorted by path.
	/// @return true iff graph hash, generator version, backend version and function list all match.
	bool IsShaderBuildCurrent(const ShaderBuildInfo& info, std::string_view platform, const SHA1Hash& graphHash, const std::vector<FunctionHash>& currentFunctions);
}
