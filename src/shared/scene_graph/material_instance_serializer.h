// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

			/// Version 0.2: adds an optional MFOL chunk carrying an instance-level terrain foliage
			/// override (replaces the parent material's foliage when present).
			Version_0_2 = 0x0200,
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

		bool ReadMaterialScalarParamChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialVectorParamChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMaterialTextureParamChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		/// @brief Reads the v0.2 instance foliage override chunk (MFOL).
		bool ReadMaterialFoliageChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		MaterialInstance& m_materialInstance;
	};
}