// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_serializer.h"
#include "material.h"

#include "base/chunk_writer.h"
#include "binary_io/writer.h"

namespace mmo
{
	static const ChunkMagic MaterialChunkMagic = { {'H', 'M', 'A', 'T'} };
	static const ChunkMagic MaterialNameChunk = { {'N', 'A', 'M', 'E'} };
	static const ChunkMagic MaterialAttributeChunk = { {'A', 'T', 'T', 'R'} };
	static const ChunkMagic MaterialVertexShaderChunk = { {'V', 'R', 'T', 'X'} };
	static const ChunkMagic MaterialPixelShaderChunk = { {'P', 'I', 'X', 'L'} };
	static const ChunkMagic MaterialTextureChunk = { {'T', 'E', 'X', 'T'} };
	
	void MaterialSerializer::Export(const Material& material, io::Writer& writer, MaterialVersion version)
	{
		if (version == material_version::Latest)
		{
			version = material_version::Version_0_1;
		}
		
		// File version chunk
		{
			ChunkWriter versionChunkWriter { MaterialChunkMagic, writer };
			writer << io::write<uint32>(version);
			versionChunkWriter.Finish();
		}

		// Material name
		{
			ChunkWriter nameChunkWriter { MaterialNameChunk, writer };
			writer << io::write_dynamic_range<uint8>(material.GetName());
			nameChunkWriter.Finish();
		}

		// Attribute chunk
		{
			MaterialAttributes attributes{};
			attributes.twoSided = material.IsTwoSided();
			attributes.castShadows = material.IsCastingShadows();
			attributes.receiveShadows = material.IsReceivingShadows();
			attributes.materialType = static_cast<uint8>(material.GetType());

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
