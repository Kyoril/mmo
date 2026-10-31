// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shader_cook.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "overlay.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "graphics/material.h"
#include "graphics/material_serializer.h"
#include "graphics/shader_compiler.h"
#include "log/default_log_levels.h"
#include "material_graph/material_graph.h"
#include "material_graph/material_shader_build.h"
#include "material_graph/material_shader_pack.h"

namespace mmo
{
	std::vector<String> ListMaterials(const std::filesystem::path& dataDir)
	{
		std::vector<String> materials;
		std::error_code error;
		for (auto it = std::filesystem::recursive_directory_iterator(dataDir, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
		{
			const std::filesystem::path relative = std::filesystem::relative(it->path(), dataDir, error);
			if (it->is_directory() && it.depth() == 0 && (relative == "Cache" || relative.string().front() == '.'))
			{
				it.disable_recursion_pending();
				continue;
			}

			if (it->is_regular_file() && it->path().extension() == ".hmat")
			{
				materials.push_back(relative.generic_string());
			}
		}

		std::sort(materials.begin(), materials.end());
		return materials;
	}

	std::filesystem::path GetShaderPackPath(const String& materialPath, const std::string_view platform)
	{
		return std::filesystem::path(materialPath + "." + String(platform) + ".shp");
	}

	bool CookShaders(const std::filesystem::path& dataDir, ShaderCompiler& compiler, const std::filesystem::path& outDir, CookReport& report)
	{
		const std::string_view platform = compiler.GetPlatform();
		CookReport stepReport;
		bool ioOk = true;

		for (const String& materialPath : ListMaterials(dataDir))
		{
			std::ifstream file(dataDir / materialPath, std::ios::binary);
			const std::vector<char> bytes { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
			if (!file && !file.eof())
			{
				ELOG("Unable to read " << materialPath);
				ioOk = false;
				continue;
			}

			Material material(materialPath);
			{
				io::MemorySource source(bytes.data(), bytes.data() + bytes.size());
				io::Reader reader(source);
				MaterialDeserializer deserializer(material);
				if (!deserializer.Read(reader))
				{
					stepReport.Warn(materialPath, "unreadable material file; keeps the repository's shaders");
					continue;
				}
			}

			const std::span<const uint8> fileBytes(reinterpret_cast<const uint8*>(bytes.data()), bytes.size());
			std::vector<uint8> graphChunk;
			if (!ReadMaterialGraphChunk(fileBytes, graphChunk))
			{
				stepReport.Warn(materialPath, "has no material graph; keeps the repository's " + String(platform) + " shaders");
				continue;
			}

			MaterialGraph graph;
			if (!LoadMaterialGraph(graphChunk, graph))
			{
				stepReport.Warn(materialPath, "material graph can't be loaded; keeps the repository's " + String(platform) + " shaders");
				continue;
			}

			const MaterialShaderBuildResult result = BuildMaterialShaders(material, graph, compiler);
			if (!result.cookable)
			{
				stepReport.Warn(materialPath, "can't be cooked for " + String(platform) + " (missing material function, no root node or a texture list that needs a resave); keeps the repository's shaders");
				continue;
			}

			if (!result.failedVariants.empty())
			{
				String names;
				for (const String& variant : result.failedVariants)
				{
					names += (names.empty() ? "" : ", ") + variant;
				}
				stepReport.Warn(materialPath, String(platform) + " shader variants failed to compile and are left empty: " + names);
			}

			ShaderPack pack;
			if (!ExtractShaderPack(material, platform, pack))
			{
				ELOG(materialPath << ": no build info after a successful build");
				ioOk = false;
				continue;
			}

			const std::filesystem::path packPath = outDir / GetShaderPackPath(materialPath, platform);
			std::error_code error;
			std::filesystem::create_directories(packPath.parent_path(), error);
			std::ofstream packFile(packPath, std::ios::binary | std::ios::trunc);
			if (!packFile)
			{
				ELOG("Unable to write " << packPath);
				ioOk = false;
				continue;
			}

			io::StreamSink sink(packFile);
			io::Writer writer(sink);
			WriteShaderPack(writer, pack);
			packFile.flush();
			if (!packFile)
			{
				ELOG("Unable to write " << packPath);
				ioOk = false;
			}
		}

		std::error_code error;
		std::filesystem::create_directories(outDir, error);
		if (!WriteWarningsFile(outDir / "warnings.json", stepReport.GetWarnings()))
		{
			ELOG("Unable to write " << (outDir / "warnings.json"));
			ioOk = false;
		}

		report.Merge(stepReport.GetWarnings());
		return ioOk;
	}
}
