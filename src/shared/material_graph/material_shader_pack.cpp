// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_shader_pack.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "graphics/material.h"

namespace mmo
{
	namespace
	{
		constexpr std::array<char, 4> PackMagic = { 'S', 'P', 'K', '1' };

		/// Upper bound for one shader's bytes, so a corrupt file can't make the reader allocate gigabytes.
		constexpr uint32 MaxShaderSize = 64u * 1024u * 1024u;
	}

	bool ExtractShaderPack(const Material& material, const std::string_view platform, ShaderPack& out)
	{
		const ShaderBuildInfo* info = material.GetShaderBuildInfo(platform);
		if (!info)
		{
			return false;
		}

		ShaderPack pack;
		pack.platform = String(platform);
		pack.buildInfo = *info;
		for (size_t i = 0; i < pack.vertexShaders.size(); ++i)
		{
			const auto code = material.GetVertexShaderCode(platform, static_cast<VertexShaderType>(i));
			pack.vertexShaders[i].assign(code.begin(), code.end());
		}
		for (size_t i = 0; i < pack.pixelShaders.size(); ++i)
		{
			const auto code = material.GetPixelShaderCode(platform, static_cast<PixelShaderType>(i));
			pack.pixelShaders[i].assign(code.begin(), code.end());
		}

		out = std::move(pack);
		return true;
	}

	void ApplyShaderPack(const ShaderPack& pack, Material& material)
	{
		for (size_t i = 0; i < pack.vertexShaders.size(); ++i)
		{
			std::vector<uint8> code = pack.vertexShaders[i];
			material.SetVertexShaderCode(pack.platform, static_cast<VertexShaderType>(i), code);
		}
		for (size_t i = 0; i < pack.pixelShaders.size(); ++i)
		{
			std::vector<uint8> code = pack.pixelShaders[i];
			material.SetPixelShaderCode(pack.platform, static_cast<PixelShaderType>(i), code);
		}

		material.SetShaderBuildInfo(pack.platform, pack.buildInfo);
		material.SetGraphHash(pack.buildInfo.graphHash);
	}

	void WriteShaderPack(io::Writer& writer, const ShaderPack& pack)
	{
		writer
			<< io::write_range(PackMagic)
			<< io::write_dynamic_range<uint8>(pack.platform)
			<< io::write_range(pack.buildInfo.graphHash)
			<< io::write<uint32>(pack.buildInfo.generatorVersion)
			<< io::write<uint32>(pack.buildInfo.backendVersion)
			<< io::write<uint16>(static_cast<uint16>(pack.buildInfo.functions.size()));
		for (const FunctionHash& function : pack.buildInfo.functions)
		{
			writer
				<< io::write_dynamic_range<uint16>(function.path)
				<< io::write_range(function.hash);
		}

		writer << io::write<uint8>(static_cast<uint8>(pack.vertexShaders.size()));
		for (const auto& code : pack.vertexShaders)
		{
			writer << io::write_dynamic_range<uint32>(code);
		}

		writer << io::write<uint8>(static_cast<uint8>(pack.pixelShaders.size()));
		for (const auto& code : pack.pixelShaders)
		{
			writer << io::write_dynamic_range<uint32>(code);
		}
	}

	bool ReadShaderPack(io::Reader& reader, ShaderPack& out)
	{
		std::array<char, 4> magic {};
		if (!(reader >> io::read_range(magic)) || magic != PackMagic)
		{
			return false;
		}

		ShaderPack pack;
		uint16 functionCount = 0;
		reader
			>> io::read_container<uint8>(pack.platform)
			>> io::read_range(pack.buildInfo.graphHash)
			>> io::read<uint32>(pack.buildInfo.generatorVersion)
			>> io::read<uint32>(pack.buildInfo.backendVersion)
			>> io::read<uint16>(functionCount);
		for (uint16 i = 0; i < functionCount && reader; ++i)
		{
			FunctionHash function;
			reader
				>> io::read_container<uint16>(function.path)
				>> io::read_range(function.hash);
			pack.buildInfo.functions.push_back(std::move(function));
		}

		uint8 vertexCount = 0;
		if (!(reader >> io::read<uint8>(vertexCount)) || vertexCount != pack.vertexShaders.size())
		{
			return false;
		}
		for (auto& code : pack.vertexShaders)
		{
			if (!(reader >> io::read_container<uint32>(code, MaxShaderSize)))
			{
				return false;
			}
		}

		uint8 pixelCount = 0;
		if (!(reader >> io::read<uint8>(pixelCount)) || pixelCount != pack.pixelShaders.size())
		{
			return false;
		}
		for (auto& code : pack.pixelShaders)
		{
			if (!(reader >> io::read_container<uint32>(code, MaxShaderSize)))
			{
				return false;
			}
		}

		out = std::move(pack);
		return true;
	}
}
