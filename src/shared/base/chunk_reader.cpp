// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chunk_reader.h"

#include "base/utilities.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ChunkReader::ChunkReader(const bool ignoreUnhandledChunks)
		: m_ignoreUnhandledChunks(ignoreUnhandledChunks)
	{

	}

	bool ChunkReader::Read(io::Reader& reader)
	{
		while(reader && !reader.getSource()->end())
		{
			uint32 chunkHeader, chunkSize;
			if (!(reader 
				>> io::read<uint32>(chunkHeader)
				>> io::read<uint32>(chunkSize)))
			{
				break;
			}

			// This marks the expected end position in the source after the chunk contents have been read.
			// Used to verify that the user read not too few or too many bytes. Also used to correct the
			// position in order to be able to continue processing more chunks.
			const auto expectedChunkEnd = reader.getSource()->position() + chunkSize;

			// Call the respective chunk handler
			const auto chunkIt = m_chunkHandlers.find(chunkHeader);
			if (chunkIt == m_chunkHandlers.end())
			{
				if (!m_ignoreUnhandledChunks)
				{
					WLOG("Unhandled chunk found: " << log_hex_digit(chunkHeader));
					return false;
				}

				reader.getSource()->seek(expectedChunkEnd);
			}
			else if (!chunkIt->second(reader, chunkHeader, chunkSize))
			{
				ELOG("Failed to read chunk " << log_hex_digit(chunkHeader) << ": Handler returned false");
				return false;
			}

			m_requiredChunkHandlers.erase(chunkHeader);

			if (reader.getSource()->position() != expectedChunkEnd)
			{
#if _DEBUG
				if (reader.getSource()->position() < expectedChunkEnd)
				{
					WLOG("Chunk handler did not read the full chunk! Chunk position: "
						<< reader.getSource()->position() << ", expected position: " << expectedChunkEnd);
				}
				else
				{
					WLOG("Chunk handler did not read too much data! Chunk position: "
						<< reader.getSource()->position() << ", expected position: " << expectedChunkEnd);
				}
				// Output a warning in debug builds
#endif

				// Fix the state
				reader.getSource()->seek(expectedChunkEnd);
			}
		}

		if (!OnReadFinished())
		{
			return false;
		}

		return IsValid();
	}
}
