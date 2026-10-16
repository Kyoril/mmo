// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "prop_kit.h"

#include "base/chunk_reader.h"

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	/// @brief Serializes a prop kit to the chunked .hkit file format.
	class PropKitSerializer
	{
	public:
		/// @brief Serializes a prop kit to a writer.
		/// @param kit The prop kit to serialize.
		/// @param writer The writer to write to.
		void Serialize(const PropKit& kit, io::Writer& writer);
	};

	/// @brief Reads a prop kit from the chunked .hkit file format. Unknown chunks are ignored.
	class PropKitDeserializer final : public ChunkReader
	{
	public:
		/// @brief Creates a deserializer filling the given kit.
		/// @param kit The kit to read into. Must outlive the deserializer.
		explicit PropKitDeserializer(PropKit& kit);

	private:
		bool ReadVersionChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadNameChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadPreviewChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadPropsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadLightsChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

	private:
		PropKit& m_kit;
	};
}
