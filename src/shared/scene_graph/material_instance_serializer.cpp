// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "material_instance_serializer.h"

#include "material_manager.h"
#include "graphics/material_instance.h"

#include "base/chunk_writer.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static const ChunkMagic MaterialInstanceChunkMagic = MakeChunkMagic('TIMH');
	static const ChunkMagic MaterialInstanceNameChunk = MakeChunkMagic('EMAN');
	static const ChunkMagic MaterialInstanceParentChunk = MakeChunkMagic('TNRP');
	static const ChunkMagic MaterialAttributeChunk = MakeChunkMagic('RTTA');
	static const ChunkMagic MaterialTextureChunk = MakeChunkMagic('TXET');
	
	MaterialInstanceDeserializer::MaterialInstanceDeserializer(MaterialInstance& materialInstance)
		: ChunkReader(true)
		, m_materialInstance(materialInstance)
	{
		AddChunkHandler(*MaterialInstanceChunkMagic, true, *this, &MaterialInstanceDeserializer::ReadMaterialInstanceChunk);
	}

	bool MaterialInstanceDeserializer::ReadMaterialInstanceChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		uint32 version;
		reader >> io::read<uint32>(version);

		if (reader)
		{
			if (version >= material_version::Version_0_1)
			{
				AddChunkHandler(*MaterialInstanceNameChunk, true, *this, &MaterialInstanceDeserializer::ReadMaterialNameChunk);
				AddChunkHandler(*MaterialInstanceParentChunk, true, *this, &MaterialInstanceDeserializer::ReadParentChunk);
				AddChunkHandler(*MaterialTextureChunk, false, *this, &MaterialInstanceDeserializer::ReadMaterialTextureChunk);
				AddChunkHandler(*MaterialAttributeChunk, false, *this, &MaterialInstanceDeserializer::ReadMaterialAttributeV2Chunk);
			}
			else
			{
				ELOG("Unknown material instance version!");
				return false;
			}
		}

		return reader;
	}

	bool MaterialInstanceDeserializer::ReadMaterialNameChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		std::string name;
		reader >> io::read_container<uint8>(name);

		if (reader && !name.empty())
		{
			m_materialInstance.SetName(name);
			return true;
		}

		return false;
	}

	bool MaterialInstanceDeserializer::ReadParentChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		std::string name;
		reader >> io::read_container<uint8>(name);

		if (reader && !name.empty())
		{
			const MaterialPtr parent = MaterialManager::Get().Load(name);
			if (!parent)
			{
				ELOG("Unable to load material instance parent by name '" << name << "'");
				return false;
			}

			m_materialInstance.SetParent(parent);
			m_materialInstance.DerivePropertiesFromParent();
			return true;
		}

		return false;
	}

	bool MaterialInstanceDeserializer::ReadMaterialAttributeV2Chunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		MaterialAttributesV2 attributes{};
		reader.readPOD(attributes);

		if (reader)
		{
			m_materialInstance.SetTwoSided(attributes.twoSided);
			m_materialInstance.SetType(static_cast<MaterialType>(attributes.materialType));
			m_materialInstance.SetReceivesShadows(attributes.receiveShadows != 0);
			m_materialInstance.SetCastShadows(attributes.castShadows != 0);
			m_materialInstance.SetDepthTestEnabled(attributes.depthTest != 0);
			m_materialInstance.SetDepthWriteEnabled(attributes.depthWrite != 0);
		}

		return reader;
	}

	bool MaterialInstanceDeserializer::ReadMaterialTextureChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize)
	{
		//m_material.ClearTextures();

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

				//m_material.AddTexture(textureFile);
			}
		}

		return reader;
	}
	
	void MaterialInstanceSerializer::Export(const MaterialInstance& materialInstance, io::Writer& writer, MaterialInstanceVersion version)
	{
		version = material_instance_version::Version_0_1;
		
		// File version chunk
		{
			ChunkWriter versionChunkWriter { MaterialInstanceChunkMagic, writer };
			writer << io::write<uint32>(version);
			versionChunkWriter.Finish();
		}

		// Material name
		{
			ChunkWriter nameChunkWriter { MaterialInstanceNameChunk, writer };
			writer << io::write_dynamic_range<uint8>(materialInstance.GetName().begin(), materialInstance.GetName().end());
			nameChunkWriter.Finish();
		}

		// Parent chunk
		{
			const std::string_view parentName = materialInstance.GetParent()->GetName();
			ChunkWriter nameChunkWriter{ MaterialInstanceParentChunk, writer };
			writer << io::write_dynamic_range<uint8>(parentName.begin(), parentName.end());
			nameChunkWriter.Finish();
		}

		// Attribute chunk
		{
			MaterialAttributesV2 attributes{};
			attributes.twoSided = materialInstance.IsTwoSided();
			attributes.castShadows = materialInstance.IsCastingShadows();
			attributes.receiveShadows = materialInstance.IsReceivingShadows();
			attributes.materialType = static_cast<uint8>(materialInstance.GetType());
			attributes.depthWrite = static_cast<uint8>(materialInstance.IsDepthWriteEnabled());
			attributes.depthTest = static_cast<uint8>(materialInstance.IsDepthTestEnabled());

			ChunkWriter attributeChunkWriter { MaterialAttributeChunk, writer };
			writer.WritePOD(attributes);
			attributeChunkWriter.Finish();
		}

		/*
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
		*/
	}
}
