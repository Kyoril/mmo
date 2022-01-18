
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

	struct MaterialAttributes
	{
		uint8 twoSided;
		uint8 castShadows;
		uint8 receiveShadows;
		uint8 materialType;
	};

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
		
		// TODO: Serialize vertex and pixel shader byte code per platform?

	}
}