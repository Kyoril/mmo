// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_shader_cache.h"

#include <fstream>
#include <sstream>

#include "material_graph_hash.h"
#include "material_shader_pack.h"
#include "base/sha1.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "graphics/material.h"
#include "log/default_log_levels.h"

namespace mmo
{
	MaterialShaderCache::MaterialShaderCache(std::filesystem::path root)
		: m_root(std::move(root))
	{
	}

	bool MaterialShaderCache::TryLoad(const String& materialPath, const std::string_view platform, const ShaderBuildInfo& expected, Material& into) const
	{
		std::ifstream file(GetFilePath(materialPath, platform), std::ios::binary);
		if (!file)
		{
			return false;
		}

		io::StreamSource source(file);
		io::Reader reader(source);

		ShaderPack pack;
		if (!ReadShaderPack(reader, pack) || pack.platform != platform)
		{
			DLOG("Ignoring unreadable shader cache entry of " << materialPath);
			return false;
		}

		// The cached shaders must have been built from the same content with the current compiler.
		if (!IsShaderBuildCurrent(pack.buildInfo, platform, expected.graphHash, expected.functions))
		{
			return false;
		}

		ApplyShaderPack(pack, into);
		return true;
	}

	void MaterialShaderCache::Store(const String& materialPath, const std::string_view platform, const Material& from) const
	{
		ShaderPack pack;
		if (!ExtractShaderPack(from, platform, pack))
		{
			WLOG("Material " << materialPath << " has no " << platform << " build info, not caching its shaders");
			return;
		}

		std::error_code error;
		std::filesystem::create_directories(m_root, error);
		if (error)
		{
			WLOG("Unable to create shader cache directory " << m_root << ": " << error.message());
			return;
		}

		// Write next to the target and rename, so a crash never leaves a truncated entry behind.
		const std::filesystem::path target = GetFilePath(materialPath, platform);
		std::filesystem::path temporary = target;
		temporary += ".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				WLOG("Unable to write shader cache file " << temporary);
				return;
			}

			io::StreamSink sink(file);
			io::Writer writer(sink);
			WriteShaderPack(writer, pack);
			file.flush();
			if (!file)
			{
				WLOG("Unable to write shader cache file " << temporary);
				return;
			}
		}

		std::filesystem::rename(temporary, target, error);
		if (error)
		{
			WLOG("Unable to store shader cache file " << target << ": " << error.message());
			std::filesystem::remove(temporary, error);
		}
	}

	std::filesystem::path MaterialShaderCache::GetFilePath(const String& materialPath, const std::string_view platform) const
	{
		const String key = materialPath + '\n' + String(platform);
		std::ostringstream name;
		sha1PrintHex(name, sha1(key.data(), key.size()));
		name << ".shp";
		return m_root / name.str();
	}
}
