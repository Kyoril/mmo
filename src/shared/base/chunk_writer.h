// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <array>

namespace io
{
	class Writer;
}

namespace mmo
{
	typedef std::array<char, 4> ChunkMagic;
	
	inline const uint32& operator*(const ChunkMagic& magic)
	{
		return *reinterpret_cast<const uint32*>(&magic[0]);
	}

	inline const ChunkMagic MakeChunkMagic(const uint32 value)
	{
		return *reinterpret_cast<const ChunkMagic*>(&value);
	}
	
	// The chunk writer
	class ChunkWriter final
		: public NonCopyable
	{
	public:
		ChunkWriter(const ChunkMagic& magic, io::Writer& writer);
		~ChunkWriter() override;
		
	public:
		void Finish();

	private:
		// Writer
		io::Writer& m_writer;
#ifdef _DEBUG
		// Whether the chunk writer has been finished.
		bool m_finished = false;
#endif
		// Offset of the size field.
		size_t m_sizeOffset = 0;
		// Offset of the content field.
		size_t m_contentOffset = 0;
	};

}
