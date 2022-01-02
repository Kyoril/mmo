// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "header_save.h"
#include "binary_io/writer.h"
#include "base/io_array.h"
#include "hpak/pre_header_save.h"
#include "hpak/pre_header.h"
#include "base/macros.h"


namespace mmo
{
	namespace hpak
	{
		namespace v1_0
		{
			HeaderSaver::HeaderSaver(io::ISink &destination)
				: m_destination(destination)
			{
				io::Writer writer(destination);
				savePreHeader(PreHeader(Version_1_0), writer);

				m_fileCountPosition = destination.Position();
				writer
				        << io::write<uint32>(0);
			}

			void HeaderSaver::finish(uint32 fileCount)
			{
				io::Writer writer(m_destination);

				writer.WritePOD(m_fileCountPosition, static_cast<uint32>(fileCount));
			}


			FileEntrySaver::FileEntrySaver(
			    io::ISink &destination,
			    const std::string &name,
			    CompressionType compression)
				: m_destination(destination)
			{
				io::Writer writer(destination);

				writer
				        << io::write_dynamic_range<uint16>(name)
				        << io::write<uint16>(compression)
				        ;

				m_offsetPosition = destination.Position();
				writer
				        << io::write<uint64>(0);

				m_sizePosition = destination.Position();
				writer
				        << io::write<uint64>(0);

				m_originalSizePosition = destination.Position();
				writer
				        << io::write<uint64>(0);

				m_digestPosition = destination.Position();
				for (size_t i = 0; i < 20; ++i)
				{
					writer << io::write<uint8>(0);
				}
			}

			void FileEntrySaver::finish(
			    uint64 offset,
			    uint64 size,
			    uint64 originalSize,
			    const SHA1Hash &digest)
			{
				io::Writer writer(m_destination);

				writer.WritePOD(m_offsetPosition,
				                static_cast<uint64>(offset));

				writer.WritePOD(m_sizePosition,
				                static_cast<uint64>(size));

				writer.WritePOD(m_originalSizePosition,
				                static_cast<uint64>(originalSize));

				ASSERT(digest.size() == 20);
				writer.Sink().Overwrite(
				    m_digestPosition,
				    reinterpret_cast<const char *>(digest.data()),
				    digest.size());
			}
		}
	}
}
