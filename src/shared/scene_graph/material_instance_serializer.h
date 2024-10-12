// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/chunk_reader.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include "material_serializer.h"

namespace io
{
	class Writer;
}

namespace mmo
{
	class MaterialInstance;

	namespace material_instance_version
	{
		enum Type
		{
			Latest = -1,

			Version_0_1 = 0x0100,
		};	
	}

	typedef material_instance_version::Type MaterialInstanceVersion;

	class MaterialInstanceSerializer
	{
	public:
		void Export(const MaterialInstance& materialInstance, io::Writer& writer, MaterialInstanceVersion version = material_instance_version::Latest);
	};
	
	/// @brief Implementation of the ChunkReader to read chunked material files.
	class MaterialInstanceDeserializer : public ChunkReader
	{
	public:
		/// @brief Creates a new instance of the MaterialChunkReader class and initializes it.
		/// @param material The material which will be updated by this reader.
		explicit MaterialInstanceDeserializer(MaterialInstance& materialInstance);

	protected:
		bool ReadMaterialInstanceChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialNameChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadParentChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialAttributeV2Chunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		
		bool ReadMaterialTextureChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		MaterialInstance& m_materialInstance;
	};
}