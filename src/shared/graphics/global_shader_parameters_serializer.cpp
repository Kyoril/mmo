// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "global_shader_parameters_serializer.h"
#include "global_shader_parameters.h"

#include "base/chunk_writer.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const ChunkMagic GlobalShaderParametersChunk = MakeChunkMagic('PSGH');
	static const ChunkMagic GlobalShaderParametersListChunk = MakeChunkMagic('RAPG');

	void GlobalShaderParametersSerializer::Export(const GlobalShaderParameters& params, io::Writer& writer,
		GlobalShaderParametersVersion version)
	{
		if (version == global_shader_parameters_version::Latest)
		{
			version = global_shader_parameters_version::Version_0_1;
		}

		// File version chunk
		{
			ChunkWriter versionChunkWriter{ GlobalShaderParametersChunk, writer };
			writer << io::write<uint32>(version);
			versionChunkWriter.Finish();
		}

		// Parameter list chunk
		{
			ChunkWriter listChunkWriter{ GlobalShaderParametersListChunk, writer };

			const auto& parameters = params.GetParameters();
			writer << io::write<uint16>(static_cast<uint16>(parameters.size()));
			for (const auto& param : parameters)
			{
				writer
					<< io::write_dynamic_range<uint8>(param.name)
					<< io::write<uint8>(static_cast<uint8>(param.type))
					<< io::write<float>(param.defaultValue.x)
					<< io::write<float>(param.defaultValue.y)
					<< io::write<float>(param.defaultValue.z)
					<< io::write<float>(param.defaultValue.w);
			}

			listChunkWriter.Finish();
		}
	}

	GlobalShaderParametersDeserializer::GlobalShaderParametersDeserializer(GlobalShaderParameters& params)
		: ChunkReader(true)
		, m_params(params)
	{
		AddChunkHandler(*GlobalShaderParametersChunk, true, *this, &GlobalShaderParametersDeserializer::ReadVersionChunk);
	}

	bool GlobalShaderParametersDeserializer::ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint32 version = 0;
		reader >> io::read<uint32>(version);
		m_version = static_cast<GlobalShaderParametersVersion>(version);

		if (m_version != global_shader_parameters_version::Version_0_1)
		{
			ELOG("Unsupported global shader parameters file version: " << version);
			return false;
		}

		// Start from a clean registry and register the parameter list handler.
		m_params.Clear();
		AddChunkHandler(*GlobalShaderParametersListChunk, true, *this, &GlobalShaderParametersDeserializer::ReadParametersChunk);

		return reader;
	}

	bool GlobalShaderParametersDeserializer::ReadParametersChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint16 numParams = 0;
		if (!(reader >> io::read<uint16>(numParams)))
		{
			return false;
		}

		for (uint16 i = 0; i < numParams; ++i)
		{
			String name;
			uint8 type = 0;
			Vector4 defaultValue;
			if (!(reader
				>> io::read_container<uint8>(name)
				>> io::read<uint8>(type)
				>> io::read<float>(defaultValue.x)
				>> io::read<float>(defaultValue.y)
				>> io::read<float>(defaultValue.z)
				>> io::read<float>(defaultValue.w)))
			{
				return false;
			}

			if (static_cast<GlobalShaderParameterType>(type) == global_shader_parameter_type::Vector)
			{
				m_params.DefineVector(name, defaultValue);
			}
			else
			{
				m_params.DefineScalar(name, defaultValue.x);
			}
		}

		return reader;
	}
}
