// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_assembly.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "overlay.h"
#include "shader_cook.h"
#include "base/chunk_writer.h"
#include "binary_io/memory_source.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "graphics/material.h"
#include "graphics/material_serializer.h"
#include "log/default_log_levels.h"
#include "material_graph/material_shader_build.h"
#include "material_graph/material_shader_pack.h"

namespace mmo
{
	namespace
	{
		/// Finds the packs of one material: every "<material>.<platform>.shp" in the pack directories.
		std::vector<std::filesystem::path> FindPacks(const String& materialPath, const std::vector<std::filesystem::path>& shaderPackDirs)
		{
			std::vector<std::filesystem::path> packs;
			const std::filesystem::path relative(materialPath);
			const String prefix = relative.filename().string() + ".";
			for (const std::filesystem::path& packDir : shaderPackDirs)
			{
				const std::filesystem::path directory = packDir / relative.parent_path();
				std::error_code error;
				for (auto it = std::filesystem::directory_iterator(directory, error); !error && it != std::filesystem::directory_iterator(); it.increment(error))
				{
					const String name = it->path().filename().string();
					if (it->is_regular_file() && name.size() > prefix.size() + 4 && name.compare(0, prefix.size(), prefix) == 0
						&& it->path().extension() == ".shp" && name.find('.', prefix.size()) == name.size() - 4)
					{
						packs.push_back(it->path());
					}
				}
			}

			std::sort(packs.begin(), packs.end());
			return packs;
		}
	}

	bool AssembleMaterials(const std::filesystem::path& dataDir, const std::vector<std::filesystem::path>& shaderPackDirs, const std::filesystem::path& overlayDir, CookReport& report)
	{
		bool ioOk = true;

		for (const std::filesystem::path& packDir : shaderPackDirs)
		{
			std::vector<CookWarning> packWarnings;
			if (ReadWarningsFile(packDir / "warnings.json", packWarnings))
			{
				report.Merge(packWarnings);
			}
			else
			{
				report.Warn(packDir.filename().generic_string(), "shader pack directory has no readable warnings.json");
			}
		}

		for (const String& materialPath : ListMaterials(dataDir))
		{
			const std::vector<std::filesystem::path> packs = FindPacks(materialPath, shaderPackDirs);
			if (packs.empty())
			{
				continue;
			}

			std::ifstream file(dataDir / materialPath, std::ios::binary);
			const std::vector<char> bytes { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };

			Material material(materialPath);
			{
				io::MemorySource source(bytes.data(), bytes.data() + bytes.size());
				io::Reader reader(source);
				MaterialDeserializer deserializer(material);
				if (!deserializer.Read(reader))
				{
					report.Warn(materialPath, "unreadable material file; not assembled");
					continue;
				}
			}

			bool packsOk = true;
			for (const std::filesystem::path& packPath : packs)
			{
				std::ifstream packFile(packPath, std::ios::binary);
				const std::vector<char> packBytes { std::istreambuf_iterator<char>(packFile), std::istreambuf_iterator<char>() };
				io::MemorySource source(packBytes.data(), packBytes.data() + packBytes.size());
				io::Reader reader(source);

				ShaderPack pack;
				if (!ReadShaderPack(reader, pack))
				{
					report.Warn(materialPath, "unreadable shader pack " + packPath.filename().string() + "; not assembled");
					packsOk = false;
					break;
				}

				ApplyShaderPack(pack, material);
			}

			if (!packsOk)
			{
				continue;
			}

			std::vector<char> output;
			{
				io::VectorSink<char> sink(output);
				io::Writer writer(sink);
				MaterialSerializer serializer;
				serializer.Export(material, writer);

				std::vector<uint8> graphChunk;
				if (ReadMaterialGraphChunk(std::span<const uint8>(reinterpret_cast<const uint8*>(bytes.data()), bytes.size()), graphChunk))
				{
					ChunkWriter chunk(ChunkMagic({ 'G', 'R', 'P', 'H' }), writer);
					writer << io::write_range(graphChunk);
					chunk.Finish();
				}
			}

			const std::filesystem::path target = overlayDir / materialPath;
			std::error_code error;
			std::filesystem::create_directories(target.parent_path(), error);
			std::ofstream outFile(target, std::ios::binary | std::ios::trunc);
			outFile.write(output.data(), static_cast<std::streamsize>(output.size()));
			if (!outFile)
			{
				ELOG("Unable to write " << target);
				ioOk = false;
			}
		}

		if (!WriteOverlayManifest(overlayDir, {}, report.GetWarnings()))
		{
			ELOG("Unable to write " << (overlayDir / "overlay.json"));
			ioOk = false;
		}

		return ioOk;
	}
}
