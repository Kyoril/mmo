// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_serializer.h"
#include "graphics/material.h"
#include "graphics/shader_platform.h"

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
	static const ChunkMagic MaterialFoliageChunk = MakeChunkMagic('LOFM');

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

				// v0.5: override shader chunk handlers with the platform-grouped versions.
				// These registrations override the handlers added above so only the v0.5
				// readers are active when reading a v0.5+ file.
				if (version >= material_version::Version_0_5)
				{
					AddChunkHandler(*MaterialVertexShaderChunk, false, *this, &MaterialDeserializer::ReadMaterialVertexShaderChunkV05);
					AddChunkHandler(*MaterialPixelShaderChunk, false, *this, &MaterialDeserializer::ReadMaterialPixelShaderChunkV05);
				}

				if (version >= material_version::Version_0_6)
				{
					AddChunkHandler(*MaterialFoliageChunk, false, *this, &MaterialDeserializer::ReadMaterialFoliageChunk);
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

		for (uint8 i = 0; i < shaderCount; ++i)
		{
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

			if (shaderProfile != shader_platform::LegacyD3DSM5)
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
					ELOG("Error while reading D3D11 vertex shader code!");
					return false;
				}

				// Legacy profile "D3D_SM5" is D3D11 bytecode.
				m_material.SetVertexShaderCode(shader_platform::D3D11, VertexShaderType::Default, { shaderCode });
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

		for (uint8 i = 0; i < shaderCount; ++i)
		{
			String shaderProfile;
			if (!(reader >> io::read_container<uint8>(shaderProfile)))
			{
				return false;
			}

			VertexShaderType shaderType;
			if (!(reader >> io::read<uint8>(shaderType)) || static_cast<uint8_t>(shaderType) >= 6)
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

			if (shaderProfile != shader_platform::LegacyD3DSM5)
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
					ELOG("Error while reading D3D11 vertex shader code!");
					return false;
				}

				// Legacy profile "D3D_SM5" is D3D11 bytecode.
				m_material.SetVertexShaderCode(shader_platform::D3D11, shaderType, { shaderCode });
			}
		}

		return reader;
	}

	bool MaterialDeserializer::ReadMaterialVertexShaderChunkV05(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint8 platformCount;
		if (!(reader >> io::read<uint8>(platformCount)))
		{
			ELOG("Failed to read vertex shader platform count for material " << m_material.GetName());
			return false;
		}

		for (uint8 p = 0; p < platformCount; ++p)
		{
			String platformId;
			if (!(reader >> io::read_container<uint8>(platformId)))
			{
				ELOG("Failed to read vertex shader platform ID for material " << m_material.GetName());
				return false;
			}

			uint8 shaderCount;
			if (!(reader >> io::read<uint8>(shaderCount)))
			{
				ELOG("Failed to read vertex shader count for platform '" << platformId << "' in material " << m_material.GetName());
				return false;
			}

			for (uint8 i = 0; i < shaderCount; ++i)
			{
				uint8 shaderTypeValue;
				if (!(reader >> io::read<uint8>(shaderTypeValue)) || shaderTypeValue >= 6)
				{
					ELOG("Invalid vertex shader type index in material " << m_material.GetName());
					return false;
				}
				const VertexShaderType shaderType = static_cast<VertexShaderType>(shaderTypeValue);

				uint32 shaderCodeSize;
				if (!(reader >> io::read<uint32>(shaderCodeSize)))
				{
					return false;
				}

				if (shaderCodeSize == 0)
				{
					continue;
				}

				std::vector<uint8> shaderCode(shaderCodeSize);
				if (!(reader >> io::read_range(shaderCode)))
				{
					ELOG("Failed to read vertex shader bytecode for platform '" << platformId << "' in material " << m_material.GetName());
					return false;
				}

				m_material.SetVertexShaderCode(platformId, shaderType, { shaderCode });
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

		for (uint8 i = 0; i < shaderCount; ++i)
		{
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

			if (shaderProfile != shader_platform::LegacyD3DSM5)
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
					ELOG("Error while reading D3D11 pixel shader code!");
					return false;
				}

				// Legacy profile "D3D_SM5" is D3D11 bytecode.
				m_material.SetPixelShaderCode(shader_platform::D3D11, shaderType, { shaderCode });
			}
		}

		return reader;
	}

	bool MaterialDeserializer::ReadMaterialPixelShaderChunkV05(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint8 platformCount;
		if (!(reader >> io::read<uint8>(platformCount)))
		{
			ELOG("Failed to read pixel shader platform count for material " << m_material.GetName());
			return false;
		}

		for (uint8 p = 0; p < platformCount; ++p)
		{
			String platformId;
			if (!(reader >> io::read_container<uint8>(platformId)))
			{
				ELOG("Failed to read pixel shader platform ID for material " << m_material.GetName());
				return false;
			}

			uint8 shaderCount;
			if (!(reader >> io::read<uint8>(shaderCount)))
			{
				ELOG("Failed to read pixel shader count for platform '" << platformId << "' in material " << m_material.GetName());
				return false;
			}

			for (uint8 i = 0; i < shaderCount; ++i)
			{
				uint8 shaderTypeValue;
				if (!(reader >> io::read<uint8>(shaderTypeValue)) || shaderTypeValue >= 4)
				{
					ELOG("Invalid pixel shader type index in material " << m_material.GetName());
					return false;
				}
				const PixelShaderType shaderType = static_cast<PixelShaderType>(shaderTypeValue);

				uint32 shaderCodeSize;
				if (!(reader >> io::read<uint32>(shaderCodeSize)))
				{
					return false;
				}

				if (shaderCodeSize == 0)
				{
					continue;
				}

				std::vector<uint8> shaderCode(shaderCodeSize);
				if (!(reader >> io::read_range(shaderCode)))
				{
					ELOG("Failed to read pixel shader bytecode for platform '" << platformId << "' in material " << m_material.GetName());
					return false;
				}

				m_material.SetPixelShaderCode(platformId, shaderType, { shaderCode });
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

	bool MaterialDeserializer::ReadMaterialFoliageChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		m_material.ClearFoliageEntries();

		uint16 numEntries;
		if (!(reader >> io::read<uint16>(numEntries)))
		{
			return false;
		}

		for (uint16 i = 0; i < numEntries; ++i)
		{
			MaterialFoliageEntry entry{};
			uint8 randomYaw = 0, alignToNormal = 0, castShadows = 0;
			if (!(reader
				>> io::read<uint8>(entry.layerIndex)
				>> io::read_container<uint16>(entry.meshPath)
				>> io::read<float>(entry.density)
				>> io::read<float>(entry.minCoverage)
				>> io::read<float>(entry.minScale)
				>> io::read<float>(entry.maxScale)
				>> io::read<float>(entry.maxSlopeAngle)
				>> io::read<float>(entry.minHeight)
				>> io::read<float>(entry.maxHeight)
				>> io::read<float>(entry.fadeStartDistance)
				>> io::read<float>(entry.fadeEndDistance)
				>> io::read<uint8>(randomYaw)
				>> io::read<uint8>(alignToNormal)
				>> io::read<uint8>(castShadows)))
			{
				return false;
			}

			entry.randomYaw = randomYaw != 0;
			entry.alignToNormal = alignToNormal != 0;
			entry.castShadows = castShadows != 0;
			m_material.AddFoliageEntry(entry);
		}

		return reader;
	}

	void MaterialSerializer::Export(const Material& material, io::Writer& writer, MaterialVersion version)
	{
		version = material_version::Version_0_6;

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
			ChunkWriter textureParamChunkWriter{ MaterialTextureParamChunk, writer };
			writer << io::write<uint8>(material.GetTextureParameters().size());
			for (const auto& param : material.GetTextureParameters())
			{
				writer
					<< io::write_dynamic_range<uint8>(param.name)
					<< io::write_dynamic_range<uint16>(param.texture);
			}

			textureParamChunkWriter.Finish();
		}

		// Texture chunk
		{
			ChunkWriter textureChunkWriter { MaterialTextureChunk, writer };
			writer << io::write<uint8>(material.GetTextureFiles().size());
			for (const auto& textureFileName : material.GetTextureFiles())
			{
				writer << io::write_dynamic_range<uint8>(textureFileName);
			}

			textureChunkWriter.Finish();
		}

		// Vertex shader chunk (v0.5 platform-grouped format)
		{
			ChunkWriter shaderChunkWriter { MaterialVertexShaderChunk, writer };

			const auto& allVS = material.GetAllVertexShaderCodes();

			// Count platforms that actually have at least one non-empty shader.
			uint8 platformCount = 0;
			for (const auto& [platformId, shaders] : allVS)
			{
				for (const auto& code : shaders)
				{
					if (!code.empty()) { ++platformCount; break; }
				}
			}
			writer << io::write<uint8>(platformCount);

			for (const auto& [platformId, shaders] : allVS)
			{
				// Skip platforms with no compiled shaders.
				bool hasAny = false;
				for (const auto& code : shaders) { if (!code.empty()) { hasAny = true; break; } }
				if (!hasAny) continue;

				writer << io::write_dynamic_range<uint8>(platformId.begin(), platformId.end());

				uint8 shaderCount = 0;
				for (const auto& code : shaders) { if (!code.empty()) ++shaderCount; }
				writer << io::write<uint8>(shaderCount);

				for (uint8 i = 0; i < static_cast<uint8>(shaders.size()); ++i)
				{
					if (shaders[i].empty()) continue;
					writer << io::write<uint8>(i);
					writer << io::write_dynamic_range<uint32>(shaders[i].begin(), shaders[i].end());
				}
			}

			shaderChunkWriter.Finish();
		}

		// Pixel shader chunk (v0.5 platform-grouped format)
		{
			ChunkWriter shaderChunkWriter { MaterialPixelShaderChunk, writer };

			const auto& allPS = material.GetAllPixelShaderCodes();

			uint8 platformCount = 0;
			for (const auto& [platformId, shaders] : allPS)
			{
				for (const auto& code : shaders)
				{
					if (!code.empty()) { ++platformCount; break; }
				}
			}
			writer << io::write<uint8>(platformCount);

			for (const auto& [platformId, shaders] : allPS)
			{
				bool hasAny = false;
				for (const auto& code : shaders) { if (!code.empty()) { hasAny = true; break; } }
				if (!hasAny) continue;

				writer << io::write_dynamic_range<uint8>(platformId.begin(), platformId.end());

				uint8 shaderCount = 0;
				for (const auto& code : shaders) { if (!code.empty()) ++shaderCount; }
				writer << io::write<uint8>(shaderCount);

				for (uint8 i = 0; i < static_cast<uint8>(shaders.size()); ++i)
				{
					if (shaders[i].empty()) continue;
					writer << io::write<uint8>(i);
					writer << io::write_dynamic_range<uint32>(shaders[i].begin(), shaders[i].end());
				}
			}

			shaderChunkWriter.Finish();
		}

		// Terrain foliage chunk (v0.6+). Only written when the material carries foliage entries.
		if (!material.GetFoliageEntries().empty())
		{
			ChunkWriter foliageChunkWriter { MaterialFoliageChunk, writer };
			writer << io::write<uint16>(static_cast<uint16>(material.GetFoliageEntries().size()));
			for (const auto& entry : material.GetFoliageEntries())
			{
				writer
					<< io::write<uint8>(entry.layerIndex)
					<< io::write_dynamic_range<uint16>(entry.meshPath.begin(), entry.meshPath.end())
					<< io::write<float>(entry.density)
					<< io::write<float>(entry.minCoverage)
					<< io::write<float>(entry.minScale)
					<< io::write<float>(entry.maxScale)
					<< io::write<float>(entry.maxSlopeAngle)
					<< io::write<float>(entry.minHeight)
					<< io::write<float>(entry.maxHeight)
					<< io::write<float>(entry.fadeStartDistance)
					<< io::write<float>(entry.fadeEndDistance)
					<< io::write<uint8>(entry.randomYaw ? 1 : 0)
					<< io::write<uint8>(entry.alignToNormal ? 1 : 0)
					<< io::write<uint8>(entry.castShadows ? 1 : 0);
			}

			foliageChunkWriter.Finish();
		}
	}
}
