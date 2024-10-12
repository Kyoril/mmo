// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/chunk_reader.h"
#include "base/typedefs.h"
#include "math/vector3.h"

namespace io
{
	class Writer;
}

namespace mmo
{
	class Material;

	namespace material_version
	{
		enum Type
		{
			Latest = -1,

			Version_0_1 = 0x0100,
			Version_0_2 = 0x0200,
			Version_0_3 = 0x0300,
		};	
	}

	typedef material_version::Type MaterialVersion;
	
	
	struct MaterialAttributes
	{
		uint8 twoSided { 0 };
		uint8 castShadows { 0 };
		uint8 receiveShadows { 0 };
		uint8 materialType { 0 };
	};
	
	struct MaterialAttributesV2
	{
		uint8 twoSided { 0 };
		uint8 castShadows { 0 };
		uint8 receiveShadows { 0 };
		uint8 materialType { 0 };
		uint8 depthWrite { 1 };
		uint8 depthTest { 1 };
	};

	class MaterialSerializer
	{
	public:
		void Export(const Material& material, io::Writer& writer, MaterialVersion version = material_version::Latest);
	};
	
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
		
		bool ReadMaterialAttributeV2Chunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		
		bool ReadMaterialVertexShaderChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialVertexShaderChunkV03(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialPixelShaderChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialTextureChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialScalarParamChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialVectorParamChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialTextureParamChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		Material& m_material;
	};
}