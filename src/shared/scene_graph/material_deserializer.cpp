
#include "material_deserializer.h"
#include "base/chunk_writer.h"
#include "material_serializer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	const ChunkMagic MaterialChunkMagic = { {'H', 'M', 'A', 'T'} };
	const ChunkMagic MaterialNameChunk = { {'N', 'A', 'M', 'E'} };
	const ChunkMagic MaterialAttributeChunk = { {'A', 'T', 'T', 'R'} };
	const ChunkMagic MaterialVertexShaderChunk = { {'V', 'R', 'T', 'X'} };
	const ChunkMagic MaterialPixelShaderChunk = { {'P', 'I', 'X', 'L'} };
	const ChunkMagic MaterialTextureChunk = { {'T', 'E', 'X', 'T'} };

	MaterialDeserializer::MaterialDeserializer(Material& material)
		: ChunkReader(true)
		, m_material(material)
	{
		AddChunkHandler(*MaterialChunkMagic, true, *this, &MaterialDeserializer::ReadMaterialChunk);
	}

	bool MaterialDeserializer::ReadMaterialChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint32 version;
		reader >> io::read<uint32>(version);

		if (reader)
		{
			if (version == material_version::Version_0_1)
			{
				AddChunkHandler(*MaterialNameChunk, true, *this, &MaterialDeserializer::ReadMaterialNameChunk);
				AddChunkHandler(*MaterialAttributeChunk, true, *this, &MaterialDeserializer::ReadMaterialAttributeChunk);
				AddChunkHandler(*MaterialVertexShaderChunk, false, SkipChunkHandler{});
				AddChunkHandler(*MaterialPixelShaderChunk, false, SkipChunkHandler{});
				AddChunkHandler(*MaterialTextureChunk, true, *this, &MaterialDeserializer::ReadMaterialTextureChunk);
			}
			else
			{
				ELOG("Unknown material version!");
				return false;
			}
		}

		return reader;
	}

	bool MaterialDeserializer::ReadMaterialNameChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		std::string name;
		reader >> io::read_container<uint8>(name);

		if (reader && !name.empty())
		{
			m_material.SetName(name);
			return true;
		}

		return false;
	}

	bool MaterialDeserializer::ReadMaterialAttributeChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		MaterialAttributes attributes{};
		reader.readPOD(attributes);

		if (reader)
		{
			m_material.SetTwoSided(attributes.twoSided);
			m_material.SetType(static_cast<MaterialType>(attributes.materialType));
			m_material.SetReceivesShadows(attributes.receiveShadows != 0);
			m_material.SetCastShadows(attributes.castShadows != 0);
		}

		return reader;
	}

	bool MaterialDeserializer::ReadMaterialVertexShaderChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		return true;
	}

	bool MaterialDeserializer::ReadMaterialPixelShaderChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		return true;
	}

	bool MaterialDeserializer::ReadMaterialTextureChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		return true;
	}
}
