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

	// The chunk writer
	class ChunkWriter
		: public NonCopyable
	{
	public:
		ChunkWriter(const ChunkMagic& magic, io::Writer& writer);
		virtual ~ChunkWriter();
		
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
