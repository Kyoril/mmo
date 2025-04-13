// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_serializer.h"
#include "graphics/material.h"

#include "base/chunk_writer.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const ChunkMagic MaterialChunkMagic = MakeChunkMagic('TAMH');
	static const ChunkMagic MaterialInstanceNameChunk = MakeChunkMagic('EMAN');
	static const ChunkMagic MaterialAttributeChunk = MakeChunkMagic('RTTA');
	static const ChunkMagic MaterialVertexShaderChunk = MakeChunkMagic('XTRV');
	static const ChunkMagic MaterialPixelShaderChunk = MakeChunkMagic('LXIP');
	static const ChunkMagic MaterialTextureChunk = MakeChunkMagic('TXET');
	static const ChunkMagic MaterialScalarParamChunk = MakeChunkMagic('RAPS');
	static const ChunkMagic MaterialVectorParamChunk = MakeChunkMagic('RAPV');
	static const ChunkMagic MaterialTextureParamChunk = MakeChunkMagic('RAPT');
	
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
		m_version = static_cast<MaterialVersion>(version);

		if (reader)
		{
			if (version >= material_version::Version_0_1)
			{
				AddChunkHandler(*MaterialInstanceNameChunk, true, *this, &MaterialDeserializer::ReadMaterialNameChunk);
				AddChunkHandler(*MaterialPixelShaderChunk, false, *this, &MaterialDeserializer::ReadMaterialPixelShaderChunk);
				AddChunkHandler(*MaterialTextureChunk, true, *this, &MaterialDeserializer::ReadMaterialTextureChunk);
				AddChunkHandler(*MaterialAttributeChunk, true, *this, &MaterialDeserializer::ReadMaterialAttributeChunk);

				if (version >= material_version::Version_0_2)
				{
					AddChunkHandler(*MaterialAttributeChunk, true, *this, &MaterialDeserializer::ReadMaterialAttributeV2Chunk);
				}

				if (version >= material_version::Version_0_3)
				{
					AddChunkHandler(*MaterialVertexShaderChunk, false, *this, &MaterialDeserializer::ReadMaterialVertexShaderChunkV03);

					AddChunkHandler(*MaterialScalarParamChunk, false, *this, &MaterialDeserializer::ReadMaterialScalarParamChunk);
					AddChunkHandler(*MaterialVectorParamChunk, false, *this, &MaterialDeserializer::ReadMaterialVectorParamChunk);
					AddChunkHandler(*MaterialTextureParamChunk, false, *this, &MaterialDeserializer::ReadMaterialTextureParamChunk);
				}
				else
				{
					AddChunkHandler(*MaterialVertexShaderChunk, false, *this, &MaterialDeserializer::ReadMaterialVertexShaderChunk);
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

				m_material.SetVertexShaderCode(VertexShaderType::Default, { shaderCode });
			}
		}

		return reader;
	}

	bool MaterialDeserializer::ReadMaterialVertexShaderChunkV03(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
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

			VertexShaderType shaderType;
			if (!(reader >> io::read<uint8>(shaderType)) || static_cast<uint8_t>(shaderType) >= 4)
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

				m_material.SetVertexShaderCode(shaderType, { shaderCode });
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

			// Read shader type (for newer versions)
			PixelShaderType shaderType = PixelShaderType::Forward;

			if (m_version >= material_version::Version_0_4)
			{
				uint8 shaderTypeValue;
				if (reader >> io::read<uint8>(shaderTypeValue))
				{
					shaderType = static_cast<PixelShaderType>(shaderTypeValue);
				}
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

				m_material.SetPixelShaderCode(shaderType, { shaderCode });
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

	bool MaterialDeserializer::ReadMaterialScalarParamChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint16 numParams;
		if (reader >> io::read<uint16>(numParams))
		{
			for (uint16 i = 0; i < numParams; ++i)
			{
				String name;
				float defaultValue;
				if ((reader >> io::read_container<uint8>(name) >> io::read<float>(defaultValue)))
				{
					m_material.AddScalarParameter(name, defaultValue);
				}
			}
		}

		return reader;
	}

	bool MaterialDeserializer::ReadMaterialVectorParamChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint16 numParams;
		if (reader >> io::read<uint16>(numParams))
		{
			for (uint16 i = 0; i < numParams; ++i)
			{
				String name;
				Vector4 defaultValue;
				if ((reader >> io::read_container<uint8>(name) 
					>> io::read<float>(defaultValue.x)
					>> io::read<float>(defaultValue.y)
					>> io::read<float>(defaultValue.z)
					>> io::read<float>(defaultValue.w)))
				{
					m_material.AddVectorParameter(name, defaultValue);
				}
			}
		}

		return reader;
	}

	bool MaterialDeserializer::ReadMaterialTextureParamChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint8 numParams;
		if (reader >> io::read<uint8>(numParams))
		{
			for (uint16 i = 0; i < numParams; ++i)
			{
				String name;
				String defaultTexture;
				if ((reader 
					>> io::read_container<uint8>(name)
					>> io::read_container<uint16>(defaultTexture)))
				{
					m_material.AddTextureParameter(name, defaultTexture);
				}
			}
		}

		return reader;
	}

	void MaterialSerializer::Export(const Material& material, io::Writer& writer, MaterialVersion version)
	{
		version = material_version::Version_0_4_1;
		
		// File version chunk
		{
			ChunkWriter versionChunkWriter { MaterialChunkMagic, writer };
			writer << io::write<uint32>(version);
			versionChunkWriter.Finish();
		}

		// Material name
		{
			ChunkWriter nameChunkWriter { MaterialInstanceNameChunk, writer };
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

		// Scalar Parameters
		if (!material.GetScalarParameters().empty())
		{
			ChunkWriter scalarParamChunkWriter{ MaterialScalarParamChunk, writer };
			writer << io::write<uint16>(material.GetScalarParameters().size());
			for (const auto& param : material.GetScalarParameters())
			{
				writer << io::write_dynamic_range<uint8>(param.name) << io::write<float>(param.value);
			}

			scalarParamChunkWriter.Finish();
		}

		// Vector Parameters
		if (!material.GetVectorParameters().empty())
		{
			ChunkWriter vectorParamChunkWriter{ MaterialVectorParamChunk, writer };
			writer << io::write<uint16>(material.GetVectorParameters().size());
			for (const auto& param : material.GetVectorParameters())
			{
				writer
					<< io::write_dynamic_range<uint8>(param.name)
					<< io::write<float>(param.value.x)
					<< io::write<float>(param.value.y)
					<< io::write<float>(param.value.z)
					<< io::write<float>(param.value.w);
			}

			vectorParamChunkWriter.Finish();
		}

		// Texture parameters
		if (!material.GetTextureParameters().empty())
		{
			ChunkWriter vectorParamChunkWriter{ MaterialTextureParamChunk, writer };
			writer << io::write<uint8>(material.GetTextureParameters().size());
			for (const auto& param : material.GetTextureParameters())
			{
				writer
					<< io::write_dynamic_range<uint8>(param.name)
					<< io::write_dynamic_range<uint16>(param.texture);
			}

			vectorParamChunkWriter.Finish();
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
			writer << io::write<uint8>(4);

			for (uint32 i = 0; i < 4; ++i)
			{
				// TODO: Shader model
				writer << io::write_dynamic_range<uint8>(String("D3D_SM5"));
				writer << io::write<uint8>(i);
				writer << io::write_dynamic_range<uint32>(material.GetVertexShaderCode(static_cast<VertexShaderType>(i)).begin(), material.GetVertexShaderCode(static_cast<VertexShaderType>(i)).end());
			}
			

			shaderChunkWriter.Finish();
		}
		
		// Pixel Shader chunk
		{
			ChunkWriter shaderChunkWriter { MaterialPixelShaderChunk, writer };

			// TODO: Number of shaders to write
			writer << io::write<uint8>(3); // Forward and GBuffer

			// Forward shader
			writer << io::write_dynamic_range<uint8>(String("D3D_SM5"));
			writer << io::write<uint8>(static_cast<uint8>(PixelShaderType::Forward));
			writer << io::write_dynamic_range<uint32>(material.GetPixelShaderCode(PixelShaderType::Forward).begin(), material.GetPixelShaderCode(PixelShaderType::Forward).end());

			// GBuffer shader
			writer << io::write_dynamic_range<uint8>(String("D3D_SM5"));
			writer << io::write<uint8>(static_cast<uint8>(PixelShaderType::GBuffer));
			writer << io::write_dynamic_range<uint32>(material.GetPixelShaderCode(PixelShaderType::GBuffer).begin(), material.GetPixelShaderCode(PixelShaderType::GBuffer).end());

			// Shadowmap shader
			writer << io::write_dynamic_range<uint8>(String("D3D_SM5"));
			writer << io::write<uint8>(static_cast<uint8>(PixelShaderType::ShadowMap));
			writer << io::write_dynamic_range<uint32>(material.GetPixelShaderCode(PixelShaderType::ShadowMap).begin(), material.GetPixelShaderCode(PixelShaderType::ShadowMap).end());

			shaderChunkWriter.Finish();
		}
	}
}
