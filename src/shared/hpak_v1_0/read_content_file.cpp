// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "read_content_file.h"
#include "header.h"

#include "zstr/zstr.hpp"


namespace mmo
{
	namespace hpak
	{
		namespace v1_0
		{
			ContentFileReader::ContentFileReader(
			    const Header &/*header*/,
			    const FileEntry &file,
			    std::istream &source
			)
			{
				const auto contentBegin = file.contentOffset;
				const auto contentLength = file.size;

				// Seek the beginning
				source.seekg(contentBegin, std::ios::beg);

				// Copy the data read
				char buffer[4096];
				std::streamsize read = 0;
				do
				{
					if (source.read(buffer, std::min<uint64>(4096, contentLength - read)))
					{
						std::streamsize r = source.gcount();
						read += r;

						m_contentStream.write(buffer, r);
					}
					else
					{
						break;
					}
				} while (source && read < static_cast<std::streamsize>(contentLength));

				ASSERT(read == contentLength);
				m_contentStream.seekg(0, std::ios::beg);
				
				// Create stream
				switch (file.compression)
				{
				case ZLibCompressed:
					m_stream = std::make_unique<zstr::istream>(m_contentStream);
					break;

				default:
					m_stream = std::make_unique<std::istream>(m_contentStream.rdbuf()/*, true*/);
					break;
				}
			}
		}
	}
}
