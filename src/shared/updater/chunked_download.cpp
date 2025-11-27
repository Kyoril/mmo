// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chunked_download.h"
#include <algorithm>

namespace mmo::updating
{
	std::vector<DownloadChunk> createChunks(
		std::uintmax_t fileSize,
		std::uintmax_t chunkSize)
	{
		std::vector<DownloadChunk> chunks;
		
		if (fileSize == 0)
		{
			return chunks;
		}
		
		std::uintmax_t offset = 0;
		while (offset < fileSize)
		{
			DownloadChunk chunk;
			chunk.start = offset;
			chunk.end = std::min(offset + chunkSize - 1, fileSize - 1);
			chunk.size = chunk.end - chunk.start + 1;
			chunk.completed = false;
			
			chunks.push_back(chunk);
			offset += chunkSize;
		}
		
		return chunks;
	}
	
	bool shouldUseChunkedDownload(
		std::uintmax_t fileSize,
		const ChunkedDownloadConfig& config)
	{
		return fileSize >= config.minFileSizeForChunking;
	}
}
