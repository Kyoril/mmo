// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "base/typedefs.h"
#include "graphics/shader_build_info.h"

namespace mmo
{
	class Material;
	class MaterialGraph;
	class ShaderCompiler;

	/// @brief Outcome of building one platform's shaders for a material.
	struct MaterialShaderBuildResult
	{
		/// @brief false if nothing could be built (no root node, a material function is missing or the
		///        texture list does not match). The material is left untouched then.
		bool cookable { false };

		/// @brief Names of the shader variants (VS0.., PS0..) that failed to compile. They are stored empty.
		std::vector<String> failedVariants;
	};

	/// @brief Generates and compiles every shader variant of a graph into a scratch material and, on
	///        success, replaces only the compiler's platform in the given material with the fresh set
	///        (empty slots for failed variants). Records that platform's shader build info and the
	///        material's graph hash.
	/// @param material The material to update. Other platforms' shaders stay as they are.
	/// @param graph The graph to build from.
	/// @param compiler The shader compiler of the platform to build for.
	/// @return The result. cookable is false and the material untouched if the build was not possible.
	MaterialShaderBuildResult BuildMaterialShaders(Material& material, const MaterialGraph& graph, ShaderCompiler& compiler);

	/// @brief Records that a platform's shaders of a material were just compiled from a graph: stores the
	///        platform's shader build info and the material's graph hash. For callers that compiled the
	///        shaders themselves.
	/// @param material The material whose shaders were compiled.
	/// @param graph The graph they were compiled from.
	/// @param platform The shader platform that was compiled.
	/// @param functions The graph's material function hashes, as collected by CollectFunctionHashes.
	void RecordShaderBuildInfo(Material& material, const MaterialGraph& graph, std::string_view platform, std::vector<FunctionHash> functions);

	/// @brief Checks whether a material's stored shaders of a platform still match its graph.
	/// @param material The material.
	/// @param graph The material's graph.
	/// @param platform The shader platform to check.
	/// @return false if no build info is stored for the platform, a function is missing or anything changed.
	bool IsMaterialShaderBuildCurrent(const Material& material, const MaterialGraph& graph, std::string_view platform);

	/// @brief Finds the GRPH chunk in the bytes of a .hmat file without loading the material.
	/// @param hmatBytes The file content.
	/// @param outGraphChunk Receives the chunk's content (the serialized graph).
	/// @return true if the chunk was found.
	bool ReadMaterialGraphChunk(std::span<const uint8> hmatBytes, std::vector<uint8>& outGraphChunk);

	/// @brief Deserializes a material graph from the content of a GRPH chunk, including its root node.
	/// @param graphChunk The chunk content.
	/// @param outGraph The graph to load into. Expected to be empty.
	/// @return true on success.
	bool LoadMaterialGraph(std::span<const uint8> graphChunk, MaterialGraph& outGraph);
}
