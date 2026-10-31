// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_shader_build.h"

#include "base/chunk_reader.h"
#include "base/chunk_writer.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "graphics/material.h"
#include "graphics/material_compiler_hlsl.h"
#include "graphics/shader_build_info.h"
#include "graphics/shader_compiler.h"
#include "log/default_log_levels.h"
#include "material_graph/material_graph.h"
#include "material_graph/material_graph_hash.h"
#include "material_graph/material_node.h"

namespace mmo
{
	namespace
	{
		constexpr uint32 VertexShaderTypeCount = 6;
		constexpr uint32 PixelShaderTypeCount = 4;
	}

	MaterialShaderBuildResult BuildMaterialShaders(Material& material, const MaterialGraph& graph, ShaderCompiler& compiler)
	{
		MaterialShaderBuildResult result;

		if (!graph.HasRootNode())
		{
			WLOG("Material " << material.GetName() << ": graph has no root node, can't build shaders");
			return result;
		}

		std::vector<FunctionHash> functions;
		if (!CollectFunctionHashes(graph, functions))
		{
			WLOG("Material " << material.GetName() << ": a material function is missing, can't build shaders");
			return result;
		}

		// Compile into a scratch material: only the platform's shaders are taken over.
		MaterialCompilerHlsl materialCompiler;
		graph.Compile(materialCompiler);

		Material scratch(material.GetName());
		materialCompiler.Compile(scratch, compiler);

		// Texture slots are assigned by the compiler, so the shaders only fit the stored
		// texture list if the graph still produces the same one.
		if (scratch.GetTextureFiles() != material.GetTextureFiles())
		{
			WLOG("Material " << material.GetName() << ": the graph produces a different texture list than the material stores; it needs to be resaved in the editor");
			return result;
		}

		const std::string_view platform = compiler.GetPlatform();

		for (uint32 i = 0; i < VertexShaderTypeCount; ++i)
		{
			const auto type = static_cast<VertexShaderType>(i);
			const auto code = scratch.GetVertexShaderCode(platform, type);
			std::vector<uint8> copy(code.begin(), code.end());
			if (copy.empty())
			{
				result.failedVariants.push_back("VS" + std::to_string(i));
			}
			material.SetVertexShaderCode(platform, type, copy);
		}

		for (uint32 i = 0; i < PixelShaderTypeCount; ++i)
		{
			const auto type = static_cast<PixelShaderType>(i);
			const auto code = scratch.GetPixelShaderCode(platform, type);
			std::vector<uint8> copy(code.begin(), code.end());
			if (copy.empty())
			{
				result.failedVariants.push_back("PS" + std::to_string(i));
			}
			material.SetPixelShaderCode(platform, type, copy);
		}

		const SHA1Hash graphHash = ComputeGraphHash(graph);

		ShaderBuildInfo info;
		info.graphHash = graphHash;
		info.generatorVersion = material_compiler_version::Generator;
		info.backendVersion = material_compiler_version::GetBackendVersion(platform);
		info.functions = std::move(functions);
		material.SetShaderBuildInfo(platform, std::move(info));
		material.SetGraphHash(graphHash);

		result.cookable = true;
		return result;
	}

	bool ReadMaterialGraphChunk(const std::span<const uint8> hmatBytes, std::vector<uint8>& outGraphChunk)
	{
		bool found = false;
		std::vector<uint8> chunk;

		const auto* hmatBegin = reinterpret_cast<const char*>(hmatBytes.data());
		io::MemorySource source(hmatBegin, hmatBegin + hmatBytes.size());
		io::Reader reader(source);

		ChunkReader chunkReader(true);
		chunkReader.AddChunkHandler(*ChunkMagic({ 'G', 'R', 'P', 'H' }), false, [&chunk, &found](io::Reader& chunkStream, uint32, const uint32 size) -> bool
		{
			chunk.resize(size);
			found = true;
			return size == 0 || (chunkStream >> io::read_range(chunk.begin(), chunk.end()));
		});

		if (!chunkReader.Read(reader) || !found)
		{
			return false;
		}

		outGraphChunk = std::move(chunk);
		return true;
	}

	bool LoadMaterialGraph(const std::span<const uint8> graphChunk, MaterialGraph& outGraph)
	{
		outGraph.GetNodeRegistry()->RegisterNodeType(MaterialNode::GetStaticTypeInfo());
		outGraph.CreateNode<MaterialNode>(true);

		ExecutableMaterialGraphLoadContext context;
		const auto* chunkBegin = reinterpret_cast<const char*>(graphChunk.data());
		io::MemorySource source(chunkBegin, chunkBegin + graphChunk.size());
		io::Reader reader(source);
		return outGraph.Deserialize(reader, context) && context.PerformAfterLoadActions();
	}
}
