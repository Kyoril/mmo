// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "hpak/magic.h"
#include "base/sha1.h"
#include "magic.h"


namespace io
{
	struct ISink;
}


namespace mmo
{
	namespace hpak
	{
		namespace v1_0
		{
			struct HeaderSaver
			{
				explicit HeaderSaver(io::ISink &destination);
				void finish(uint32 fileCount);

			private:

				io::ISink &m_destination;
				std::size_t m_fileCountPosition;
			};


			struct FileEntrySaver
			{
				explicit FileEntrySaver(
				    io::ISink &destination,
				    const std::string &name,
				    CompressionType compression
				);

				void finish(
				    uint64 offset,
				    uint64 size,
				    uint64 originalSize,
				    const SHA1Hash &digest
				);

			private:

				io::ISink &m_destination;
				std::size_t m_offsetPosition;
				std::size_t m_sizePosition;
				std::size_t m_originalSizePosition;
				std::size_t m_digestPosition;
			};
		}
	}
}
