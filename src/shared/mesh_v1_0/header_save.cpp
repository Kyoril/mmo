// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "header_save.h"
#include "binary_io/writer.h"
#include "base/io_array.h"
#include "mesh/pre_header_save.h"
#include "mesh/pre_header.h"
#include "base/macros.h"
#include "header.h"


namespace mmo
{
	namespace mesh
	{
		namespace v1_0
		{
			HeaderSaver::HeaderSaver(io::ISink &destination, const Header& header)
				: m_destination(destination)
				, m_header(header)
#ifdef _DEBUG
				, m_finished(false)
#endif
			{
				io::Writer writer(destination);
				SavePreHeader(PreHeader(m_header.version), writer);

				m_vertexChunkSizePos = destination.Position();
				writer
					<< io::write<uint32>(m_header.vertexChunkOffset);

				m_indexChunkSizePos = destination.Position();
				writer
					<< io::write<uint32>(m_header.indexChunkOffset);
			}

			HeaderSaver::~HeaderSaver()
			{
#ifdef _DEBUG
				ASSERT(m_finished);
#endif
			}

			void HeaderSaver::Finish()
			{
				io::Writer writer(m_destination);

				if (m_header.vertexChunkOffset != 0)
				{
					writer.WritePOD(m_vertexChunkSizePos, m_header.vertexChunkOffset);
				}

				if (m_header.indexChunkOffset != 0)
				{
					writer.WritePOD(m_indexChunkSizePos, m_header.indexChunkOffset);
				}
#ifdef _DEBUG
				m_finished = true;
#endif
			}

			SubMeshChunkSaver::SubMeshChunkSaver(io::ISink & destination)
				: m_destination(destination)
			{
				io::Writer writer{ destination };
				writer
					<< io::write_range(SubMeshChunkMagic);

				m_chunkSizePos = destination.Position();
				writer
					<< io::write<uint32>(0);

				m_contentPos = destination.Position();
			}

			void SubMeshChunkSaver::Finish()
			{
				const uint32 contentDiff = static_cast<uint32>(m_destination.Position() - m_contentPos);

				io::Writer writer{ m_destination };
				writer.WritePOD(m_chunkSizePos, contentDiff);
			}
		}
	}
}
