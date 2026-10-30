// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Adds Metal shaders to material files (.hmat).
//
// Material files carry compiled shaders per platform next to the material graph they were
// compiled from. The editor only produces D3D11 bytecode, so this tool recompiles each material's
// graph with the HLSL generator, cross compiles the HLSL to Metal (msl_cross) and stores the result
// as the "METAL" platform. Everything else in the file - the D3D11 shaders, textures, parameters
// and the graph chunk - is written back unchanged.
//
// Usage: metal_material_baker <data directory> [--force] [--check] [--dump <dir>] [material paths relative to the data directory...]
//   Without material paths, every .hmat below the data directory is processed.
//   --force  Rebakes materials that already contain Metal shaders.
//   --check  Only reports which materials lack Metal shaders; exits with 1 if any do.
//   --dump   Writes the generated HLSL of every baked material to the given directory.

#include "assets/asset_registry.h"
#include "base/chunk_writer.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "editors/material_editor/material_graph.h"
#include "editors/material_editor/material_node.h"
#include "graphics/global_shader_parameters.h"
#include "graphics/graphics_device.h"
#include "graphics/material.h"
#include "graphics/shader_compiler.h"
#include "graphics/shader_platform.h"
#include "graphics_d3d11/material_compiler_d3d11.h"
#include "log/default_log.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "msl_cross/msl_cross.h"
#include "scene_graph/material_serializer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

namespace mmo
{
	/// Compiles the HLSL the material compiler generates to Metal shader blobs.
	class ShaderCompilerMetal final : public ShaderCompiler
	{
	private:
		/// The generated code defines a scalar select() helper and sometimes passes vectors to it,
		/// which fxc truncates with a warning but glslang rejects. Declares the truncating overloads
		/// explicitly, so both compilers produce the same result.
		static String AddSelectOverloads(const String& source)
		{
			static const String selectDefinition = "float select(bool expression, float whenTrue, float whenFalse) {";
			const size_t position = source.find(selectDefinition);
			if (position == String::npos)
			{
				return source;
			}

			static const char* types[] = { "float", "float2", "float3", "float4" };
			std::ostringstream overloads;
			for (const char* whenTrue : types)
			{
				for (const char* whenFalse : types)
				{
					if (whenTrue == types[0] && whenFalse == types[0])
					{
						continue;
					}

					overloads << "float select(bool expression, " << whenTrue << " whenTrue, " << whenFalse << " whenFalse) { return expression ? (float)whenTrue : (float)whenFalse; }\n";
				}
			}

			String result = source;
			result.insert(position, overloads.str());
			return result;
		}

	public:
		[[nodiscard]] const String& GetShaderFormat() const override
		{
			static const String format(shader_platform::Metal);
			return format;
		}

		void Compile(const ShaderCompileInput& input, ShaderCompileResult& output) override
		{
			msl_cross::CompileInput crossInput;
			crossInput.source = AddSelectOverloads(input.shaderCode);
			crossInput.sourceName = "material.hlsl";
			switch (input.shaderType)
			{
			case ShaderType::PixelShader:
				crossInput.stage = metal_shader_blob::Stage::Pixel;
				break;
			case ShaderType::ComputeShader:
				crossInput.stage = metal_shader_blob::Stage::Compute;
				break;
			default:
				crossInput.stage = metal_shader_blob::Stage::Vertex;
				break;
			}

			msl_cross::CompileOutput crossOutput;
			output.succeeded = msl_cross::CompileHlslToMsl(crossInput, crossOutput);
			output.code.format = GetShaderFormat();
			if (output.succeeded)
			{
				output.code.data = crossOutput.ToBlob(crossInput.stage);
			}
			else
			{
				output.code.data.clear();
				output.errorMessage = crossOutput.log;
			}
		}
	};
}

namespace
{
	using namespace mmo;

	constexpr uint32 VertexShaderTypeCount = 6;
	constexpr uint32 PixelShaderTypeCount = 4;

	bool HasMetalShaders(const Material& material)
	{
		return !material.GetVertexShaderCode(shader_platform::Metal, VertexShaderType::Default).empty();
	}

	enum class BakeResult
	{
		Baked,
		Skipped,
		Missing,
		Failed
	};

	/// Where to write the generated HLSL for inspection (--dump), or empty.
	std::filesystem::path s_dumpDirectory;

	void DumpGeneratedCode(const std::string& assetPath, const MaterialCompiler& compiler)
	{
		if (s_dumpDirectory.empty())
		{
			return;
		}

		std::string baseName = assetPath;
		std::replace(baseName.begin(), baseName.end(), '/', '_');
		std::filesystem::create_directories(s_dumpDirectory);
		for (uint32 i = 0; i < VertexShaderTypeCount; ++i)
		{
			std::ofstream(s_dumpDirectory / (baseName + ".vs" + std::to_string(i) + ".hlsl")) << compiler.GetVertexShaderCode(static_cast<VertexShaderType>(i));
		}
		for (uint32 i = 0; i < PixelShaderTypeCount; ++i)
		{
			std::ofstream(s_dumpDirectory / (baseName + ".ps" + std::to_string(i) + ".hlsl")) << compiler.GetPixelShaderCode(static_cast<PixelShaderType>(i));
		}
	}

	BakeResult BakeMaterial(const std::filesystem::path& dataPath, const std::string& assetPath, const bool force, const bool checkOnly)
	{
		const std::filesystem::path filePath = dataPath / assetPath;

		std::vector<char> fileContent;
		{
			std::ifstream file(filePath, std::ios::binary);
			if (!file)
			{
				ELOG(assetPath << ": unable to open");
				return BakeResult::Failed;
			}
			fileContent.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}

		// Load the material and keep the graph chunk as it is: the graph also stores the editor
		// layout, which only round-trips through a live node editor.
		auto material = std::make_shared<Material>(assetPath);
		std::vector<char> graphChunk;
		bool hasGraph = false;
		{
			io::MemorySource source(fileContent.data(), fileContent.data() + fileContent.size());
			io::Reader reader(source);

			MaterialDeserializer deserializer(*material);
			deserializer.AddChunkHandler(*ChunkMagic({ 'G', 'R', 'P', 'H' }), false, [&graphChunk, &hasGraph](io::Reader& chunkReader, uint32, const uint32 size) -> bool
			{
				graphChunk.resize(size);
				hasGraph = true;
				return size == 0 || (chunkReader >> io::read_range(graphChunk.begin(), graphChunk.end()));
			});

			if (!deserializer.Read(reader))
			{
				ELOG(assetPath << ": unable to read material");
				return BakeResult::Failed;
			}
		}

		if (HasMetalShaders(*material) && !force)
		{
			return BakeResult::Skipped;
		}

		if (checkOnly)
		{
			WLOG(assetPath << ": no Metal shaders");
			return BakeResult::Missing;
		}

		if (!hasGraph)
		{
			ELOG(assetPath << ": has no material graph to compile Metal shaders from");
			return BakeResult::Failed;
		}

		MaterialGraph graph;
		graph.GetNodeRegistry()->RegisterNodeType(MaterialNode::GetStaticTypeInfo());
		graph.CreateNode<MaterialNode>(true);

		ExecutableMaterialGraphLoadContext context;
		{
			io::MemorySource source(graphChunk.data(), graphChunk.data() + graphChunk.size());
			io::Reader reader(source);
			if (!graph.Deserialize(reader, context) || !context.PerformAfterLoadActions())
			{
				ELOG(assetPath << ": unable to read the material graph");
				return BakeResult::Failed;
			}
		}

		// Compile into a scratch material: only its Metal shaders are taken over.
		MaterialCompilerD3D11 materialCompiler;
		graph.Compile(materialCompiler);

		const auto compiledMaterial = std::make_shared<Material>(assetPath);
		Material& compiled = *compiledMaterial;
		ShaderCompilerMetal shaderCompiler;
		materialCompiler.Compile(compiled, shaderCompiler);
		DumpGeneratedCode(assetPath, materialCompiler);

		if (compiled.GetVertexShaderCode(shader_platform::Metal, VertexShaderType::Default).empty())
		{
			ELOG(assetPath << ": Metal shader compilation failed");
			return BakeResult::Failed;
		}

		// Texture slots are assigned by the compiler, so the Metal shaders only fit the stored
		// texture list if the graph still produces the same one.
		if (compiled.GetTextureFiles() != material->GetTextureFiles())
		{
			WLOG(assetPath << ": the graph produces a different texture list than the file stores; the material may need to be resaved in the editor");
		}

		for (uint32 i = 0; i < VertexShaderTypeCount; ++i)
		{
			const auto type = static_cast<VertexShaderType>(i);
			const auto code = compiled.GetVertexShaderCode(shader_platform::Metal, type);
			std::vector<uint8> copy(code.begin(), code.end());
			material->SetVertexShaderCode(shader_platform::Metal, type, copy);
		}

		for (uint32 i = 0; i < PixelShaderTypeCount; ++i)
		{
			const auto type = static_cast<PixelShaderType>(i);
			const auto code = compiled.GetPixelShaderCode(shader_platform::Metal, type);
			std::vector<uint8> copy(code.begin(), code.end());
			material->SetPixelShaderCode(shader_platform::Metal, type, copy);
		}

		std::vector<char> output;
		{
			io::VectorSink<char> sink(output);
			io::Writer writer(sink);

			MaterialSerializer serializer;
			serializer.Export(*material, writer);

			ChunkWriter graphWriter(ChunkMagic({ 'G', 'R', 'P', 'H' }), writer);
			if (!graphChunk.empty())
			{
				writer << io::write_range(graphChunk);
			}
			graphWriter.Finish();
		}

		std::ofstream outFile(filePath, std::ios::binary | std::ios::trunc);
		if (!outFile)
		{
			ELOG(assetPath << ": unable to write");
			return BakeResult::Failed;
		}
		outFile.write(output.data(), static_cast<std::streamsize>(output.size()));

		ILOG(assetPath << ": baked Metal shaders");
		return BakeResult::Baked;
	}
}

int main(const int argc, char* argv[])
{
	using namespace mmo;

	std::mutex logMutex;
	g_DefaultLog.signal().connect([&logMutex](const LogEntry& entry)
	{
		if (entry.level == &DebugLevel)
		{
			return;
		}

		std::scoped_lock lock{ logMutex };
		printLogEntry(std::cout, entry, g_DefaultConsoleLogOptions);
	});

	if (argc < 2)
	{
		std::cerr << "Usage: metal_material_baker <data directory> [--force] [--check] [materials...]" << std::endl;
		return 1;
	}

	const std::filesystem::path dataPath = std::filesystem::absolute(argv[1]);
	bool force = false;
	bool checkOnly = false;
	std::vector<std::string> materials;
	for (int i = 2; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument == "--force")
		{
			force = true;
		}
		else if (argument == "--check")
		{
			checkOnly = true;
		}
		else if (argument == "--dump" && i + 1 < argc)
		{
			s_dumpDirectory = argv[++i];
		}
		else
		{
			materials.push_back(argument);
		}
	}

	if (materials.empty())
	{
		for (const auto& entry : std::filesystem::recursive_directory_iterator(dataPath))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".hmat")
			{
				materials.push_back(std::filesystem::relative(entry.path(), dataPath).generic_string());
			}
		}
		std::sort(materials.begin(), materials.end());
	}

	// Material functions referenced by graphs are loaded through the asset registry.
	AssetRegistry::Initialize(dataPath, {});

	GraphicsDeviceDesc desc;
	GraphicsDevice::CreateNull(desc);

	// Generated shaders declare the project wide global parameters they reference.
	GlobalShaderParameters::Get().LoadFromAsset(GlobalShaderParametersAssetPath);

	uint32 baked = 0;
	uint32 skipped = 0;
	uint32 missing = 0;
	uint32 failed = 0;
	for (const std::string& material : materials)
	{
		switch (BakeMaterial(dataPath, material, force, checkOnly))
		{
		case BakeResult::Baked: ++baked; break;
		case BakeResult::Skipped: ++skipped; break;
		case BakeResult::Missing: ++missing; break;
		case BakeResult::Failed: ++failed; break;
		}
	}

	ILOG("Materials: " << baked << " baked, " << skipped << " already had Metal shaders, " << missing << " without Metal shaders, " << failed << " failed");

	GraphicsDevice::Destroy();
	AssetRegistry::Destroy();

	return (failed > 0 || missing > 0) ? 1 : 0;
}
