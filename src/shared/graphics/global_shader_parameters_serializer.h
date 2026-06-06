// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/chunk_reader.h"
#include "base/typedefs.h"

namespace io
{
	class Writer;
}

namespace mmo
{
	class GlobalShaderParameters;

	namespace global_shader_parameters_version
	{
		enum Type
		{
			Latest = -1,

			Version_0_1 = 0x0100,
		};
	}

	typedef global_shader_parameters_version::Type GlobalShaderParametersVersion;

	/// @brief Serializes a @ref GlobalShaderParameters registry into a chunked .hgsp file.
	class GlobalShaderParametersSerializer
	{
	public:
		void Export(const GlobalShaderParameters& params, io::Writer& writer,
			GlobalShaderParametersVersion version = global_shader_parameters_version::Latest);
	};

	/// @brief Reads a chunked .hgsp file into a @ref GlobalShaderParameters registry.
	class GlobalShaderParametersDeserializer final : public ChunkReader
	{
	public:
		explicit GlobalShaderParametersDeserializer(GlobalShaderParameters& params);

	protected:
		bool ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadParametersChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		GlobalShaderParameters& m_params;
		GlobalShaderParametersVersion m_version { global_shader_parameters_version::Latest };
	};
}
