// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "header_save.h"
#include "binary_io/writer.h"
#include "base/io_array.h"
#include "tex/pre_header_save.h"
#include "tex/pre_header.h"
#include "base/macros.h"
#include "header.h"


namespace mmo
{
	namespace tex
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
				savePreHeader(PreHeader(Version_1_0), writer);

				writer
					<< io::write<uint8>(header.format)
					<< io::write<uint8>(header.hasMips)
					<< io::write<uint16>(header.width)
					<< io::write<uint16>(header.height);

				m_mipPosition = writer.Sink().Position();
				writer
					<< io::write_range(header.mipmapOffsets)
					<< io::write_range(header.mipmapLengths);

				// Remember content position
				m_contentPosition = writer.Sink().Position();
			}

			HeaderSaver::~HeaderSaver()
			{
#ifdef _DEBUG
				ASSERT(m_finished);
#endif
			}

			void HeaderSaver::finish()
			{
				io::Writer writer(m_destination);

				// Remember current position
				size_t offset = m_mipPosition;

				// Rewrite mip offsets and lengths again
				for (const auto& mipOffset : m_header.mipmapOffsets)
				{
					writer.WritePOD(offset, mipOffset);
					offset += sizeof(mipOffset);
				}

				// Rewrite mip offsets and lengths again
				for (const auto& mipLength : m_header.mipmapLengths)
				{
					writer.WritePOD(offset, mipLength);
					offset += sizeof(mipLength);
				}

#ifdef _DEBUG
				m_finished = true;
#endif
			}
		}
	}
}
