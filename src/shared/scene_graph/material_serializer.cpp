// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_serializer.h"
#include "graphics/material.h"

#include "base/chunk_writer.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const ChunkMagic MaterialChunkMagic = MakeChunkMagic('TAMH');
	static const ChunkMagic MaterialNameChunk = MakeChunkMagic('EMAN');
	static const ChunkMagic MaterialAttributeChunk = MakeChunkMagic('RTTA');
	static const ChunkMagic MaterialVertexShaderChunk = MakeChunkMagic('XTRV');
	static const ChunkMagic MaterialPixelShaderChunk = MakeChunkMagic('LXIP');
	static const ChunkMagic MaterialTextureChunk = MakeChunkMagic('TXET');
	
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
			if (version >= material_version::Version_0_1)
			{
				AddChunkHandler(*MaterialNameChunk, true, *this, &MaterialDeserializer::ReadMaterialNameChunk);
				AddChunkHandler(*MaterialVertexShaderChunk, false, *this, &MaterialDeserializer::ReadMaterialVertexShaderChunk);
				AddChunkHandler(*MaterialPixelShaderChunk, false, *this, &MaterialDeserializer::ReadMaterialPixelShaderChunk);
				AddChunkHandler(*MaterialTextureChunk, true, *this, &MaterialDeserializer::ReadMaterialTextureChunk);
				AddChunkHandler(*MaterialAttributeChunk, true, *this, &MaterialDeserializer::ReadMaterialAttributeChunk);

				if (version >= material_version::Version_0_2)
				{
					AddChunkHandler(*MaterialAttributeChunk, true, *this, &MaterialDeserializer::ReadMaterialAttributeV2Chunk);
				}
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

	bool MaterialDeserializer::ReadMaterialAttributeV2Chunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		MaterialAttributesV2 attributes{};
		reader.readPOD(attributes);

		if (reader)
		{
			m_material.SetTwoSided(attributes.twoSided);
			m_material.SetType(static_cast<MaterialType>(attributes.materialType));
			m_material.SetReceivesShadows(attributes.receiveShadows != 0);
			m_material.SetCastShadows(attributes.castShadows != 0);
			m_material.SetDepthTestEnabled(attributes.depthTest != 0);
			m_material.SetDepthWriteEnabled(attributes.depthWrite != 0);
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

				m_material.SetVertexShaderCode({ shaderCode });
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

				m_material.SetPixelShaderCode({ shaderCode });
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
	
	void MaterialSerializer::Export(const Material& material, io::Writer& writer, MaterialVersion version)
	{
		version = material_version::Version_0_2;
		
		// File version chunk
		{
			ChunkWriter versionChunkWriter { MaterialChunkMagic, writer };
			writer << io::write<uint32>(version);
			versionChunkWriter.Finish();
		}

		// Material name
		{
			ChunkWriter nameChunkWriter { MaterialNameChunk, writer };
			writer << io::write_dynamic_range<uint8>(material.GetName().begin(), material.GetName().end());
			nameChunkWriter.Finish();
		}

		// Attribute chunk
		{
			MaterialAttributesV2 attributes{};
			attributes.twoSided = material.IsTwoSided();
			attributes.castShadows = material.IsCastingShadows();
			attributes.receiveShadows = material.IsReceivingShadows();
			attributes.materialType = static_cast<uint8>(material.GetType());
			attributes.depthWrite = static_cast<uint8>(material.IsDepthWriteEnabled());
			attributes.depthTest = static_cast<uint8>(material.IsDepthTestEnabled());

			ChunkWriter attributeChunkWriter { MaterialAttributeChunk, writer };
			writer.WritePOD(attributes);
			attributeChunkWriter.Finish();
		}

		// Texture chunk
		{
			ChunkWriter textureChunkWriter { MaterialTextureChunk, writer };
			writer << io::write<uint8>(material.GetTextureFiles().size());
			for(const auto& textureFileName : material.GetTextureFiles())
			{
				writer << io::write_dynamic_range<uint8>(textureFileName);
			}

			textureChunkWriter.Finish();
		}

		// Vertex Shader chunk
		{
			ChunkWriter shaderChunkWriter { MaterialVertexShaderChunk, writer };

			// TODO: Number of shaders to write
			writer << io::write<uint8>(1);

			// TODO: Shader model
			writer << io::write_dynamic_range<uint8>(String("D3D_SM5"));
			writer << io::write_dynamic_range<uint32>(material.GetVertexShaderCode().begin(), material.GetVertexShaderCode().end());

			shaderChunkWriter.Finish();
		}
		
		// Pixel Shader chunk
		{
			ChunkWriter shaderChunkWriter { MaterialPixelShaderChunk, writer };

			// TODO: Number of shaders to write
			writer << io::write<uint8>(1);

			// TODO: Shader model
			writer << io::write_dynamic_range<uint8>(String("D3D_SM5"));
			writer << io::write_dynamic_range<uint32>(material.GetPixelShaderCode().begin(), material.GetPixelShaderCode().end());
			
			shaderChunkWriter.Finish();
		}
	}
}
