#pragma once

#include "base/chunk_reader.h"

#include "material.h"
#include "base/chunk_writer.h"

namespace mmo
{
	extern const ChunkMagic MaterialChunkMagic;
	extern const ChunkMagic MaterialNameChunk;
	extern const ChunkMagic MaterialAttributeChunk;
	extern const ChunkMagic MaterialVertexShaderChunk;
	extern const ChunkMagic MaterialPixelShaderChunk;
	extern const ChunkMagic MaterialTextureChunk;

	/// @brief Implementation of the ChunkReader to read chunked material files.
	class MaterialDeserializer : public ChunkReader
	{
	public:
		/// @brief Creates a new instance of the MaterialChunkReader class and initializes it.
		/// @param material The material which will be updated by this reader.
		explicit MaterialDeserializer(Material& material);

	protected:
		bool ReadMaterialChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialNameChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialAttributeChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		
		bool ReadMaterialVertexShaderChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialPixelShaderChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialTextureChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		
	private:
		Material& m_material;
	};
}
