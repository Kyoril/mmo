
#include "chunk_writer.h"


#include "base/macros.h"
#include "binary_io/writer.h"


namespace mmo
{
	ChunkWriter::ChunkWriter(const ChunkMagic& magic, io::Writer& writer)
		: m_writer(writer)
	{
		m_writer.Sink().Write(magic.data(), magic.size());
		m_sizeOffset = m_writer.Sink().Position();

		m_writer << io::write<uint32>(0);
		m_contentOffset = m_writer.Sink().Position();
	}

	ChunkWriter::~ChunkWriter()
	{
#ifdef _DEBUG
		ASSERT(m_finished);
#endif
	}

	void ChunkWriter::Finish()
	{
		const size_t endPosition = m_writer.Sink().Position();
		ASSERT(endPosition >= m_contentOffset);

		// Write actual chunk size to size offset
		const uint32 chunkContentSize = static_cast<uint32>(endPosition - m_contentOffset);
		m_writer.WritePOD(m_sizeOffset, chunkContentSize);

#ifdef _DEBUG
		ASSERT(!m_finished);
		m_finished = true;
#endif
	}
}
