// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "base/typedefs.h"
#include "graphics/shader_build_info.h"

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	class Material;

	/// @brief One platform's compiled shaders of a material together with the build info they were made
	///        from. Used by the development client's shader cache and by the cook's .shp files.
	struct ShaderPack
	{
		/// @brief Shader platform id (see shader_platform.h).
		String platform;

		/// @brief What the shaders were built from.
		ShaderBuildInfo buildInfo;

		/// @brief Vertex shader bytes per VertexShaderType; empty for variants without a shader.
		std::array<std::vector<uint8>, 6> vertexShaders;

		/// @brief Pixel shader bytes per PixelShaderType; empty for variants without a shader.
		std::array<std::vector<uint8>, 4> pixelShaders;
	};

	/// @brief Takes one platform's shaders and build info out of a material.
	/// @param material The material.
	/// @param platform The shader platform.
	/// @param out Receives the pack.
	/// @return false if the material has no build info for the platform.
	bool ExtractShaderPack(const Material& material, std::string_view platform, ShaderPack& out);

	/// @brief Replaces the pack's platform shaders and build info in a material and sets the material's
	///        graph hash to the pack's. Other platforms stay as they are.
	/// @param pack The pack.
	/// @param material The material to update.
	void ApplyShaderPack(const ShaderPack& pack, Material& material);

	/// @brief Writes a pack: magic "SPK1", the platform, the build info and all variants in type order.
	/// @param writer The writer.
	/// @param pack The pack.
	void WriteShaderPack(io::Writer& writer, const ShaderPack& pack);

	/// @brief Reads a pack written by WriteShaderPack.
	/// @param reader The reader.
	/// @param out Receives the pack.
	/// @return false on a wrong magic or truncated data.
	bool ReadShaderPack(io::Reader& reader, ShaderPack& out);
}
