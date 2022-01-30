
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
				AddChunkHandler(*MaterialVertexShaderChunk, false, *this, &MaterialDeserializer::ReadMaterialVertexShaderChunk);
				AddChunkHandler(*MaterialPixelShaderChunk, false, *this, &MaterialDeserializer::ReadMaterialPixelShaderChunk);
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
		uint8 shaderCount;
		if (!(reader >> io::read<uint8>(shaderCount)))
		{
			ELOG("Failed to read vertex shader chunk for material " << m_material.GetName());
			return false;
		}

		if (shaderCount == 0)
		{
			WLOG("Material " << m_material.GetName() << " has no compiled vertex shaders available and might not be used in game client");
			return true;
		}

		// Read shader code
		for (uint8 i = 0; i < shaderCount; ++i)
		{
			// TODO: Right now, we only care for hardcoded D3D_SM5 shader profile
			String shaderProfile;
			if (!(reader >> io::read_container<uint8>(shaderProfile)))
			{
				return false;
			}

			uint32 shaderCodeSize;
			if (!(reader >> io::read<uint32>(shaderCodeSize)))
			{
				return false;
			}

			if (shaderCodeSize == 0)
			{
				continue;
			}

			if (shaderProfile != "D3D_SM5")
			{
				DLOG("Found shader profile " << shaderProfile << " which is currently ignored");
				reader >> io::skip(shaderCodeSize);
			}
			else
			{
				std::vector<uint8> shaderCode;
				shaderCode.resize(shaderCodeSize);
				if (!(reader >> io::read_range(shaderCode)))
				{
					ELOG("Error while reading D3D_SM5 vertex shader code!");
					return false;
				}

				m_material.SetVertexShaderCode({ shaderCode.begin(), shaderCode.end() });
			}
		}

		return reader;
	}

	bool MaterialDeserializer::ReadMaterialPixelShaderChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint8 shaderCount;
		if (!(reader >> io::read<uint8>(shaderCount)))
		{
			ELOG("Failed to read pixel shader chunk for material " << m_material.GetName());
			return false;
		}

		if (shaderCount == 0)
		{
			WLOG("Material " << m_material.GetName() << " has no compiled pixel shaders available and might not be used in game client");
			return true;
		}

		// Read shader code
		for (uint8 i = 0; i < shaderCount; ++i)
		{
			// TODO: Right now, we only care for hardcoded D3D_SM5 shader profile
			String shaderProfile;
			if (!(reader >> io::read_container<uint8>(shaderProfile)))
			{
				return false;
			}

			uint32 shaderCodeSize;
			if (!(reader >> io::read<uint32>(shaderCodeSize)))
			{
				return false;
			}

			if (shaderCodeSize == 0)
			{
				continue;
			}

			if (shaderProfile != "D3D_SM5")
			{
				DLOG("Found shader profile " << shaderProfile << " which is currently ignored");
				reader >> io::skip(shaderCodeSize);
			}
			else
			{
				std::vector<uint8> shaderCode;
				shaderCode.resize(shaderCodeSize);
				if (!(reader >> io::read_range(shaderCode)))
				{
					ELOG("Error while reading D3D_SM5 pixel shader code!");
					return false;
				}

				m_material.SetPixelShaderCode({ shaderCode.begin(), shaderCode.end() });
			}
		}

		return reader;
	}

	bool MaterialDeserializer::ReadMaterialTextureChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		m_material.ClearTextures();

		uint8 numTextures;
		if ((reader >> io::read<uint8>(numTextures)))
		{
			for (uint8 i = 0; i < numTextures; ++i)
			{
				String textureFile;
				if (!(reader >> io::read_container<uint8>(textureFile)))
				{
					break;
				}

				m_material.AddTexture(textureFile);
			}
		}

		return reader;
	}
}
