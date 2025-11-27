// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "https_update_source.h"
#include "chunked_download.h"
#include "checkpoint_manager.h"
#include <memory>

namespace mmo::updating
{
	/// Enhanced HTTPS update source with chunked download support
	struct ChunkedHTTPSUpdateSource : HTTPSUpdateSource
	{
		explicit ChunkedHTTPSUpdateSource(
		    std::string host,
		    uint16 port,
		    std::string path,
		    ChunkedDownloadConfig config = ChunkedDownloadConfig{}
		);

		/// Download a file with chunked download support if file is large enough
		UpdateSourceFile readFile(
		    const std::string &path
		) override;
		
		/// Download a file using chunked parallel download
		UpdateSourceFile readFileChunked(
		    const std::string &path,
		    std::uintmax_t fileSize,
		    ChunkProgressCallback progressCallback = nullptr
		);
		
		/// Download a single chunk
		std::vector<char> downloadChunk(
		    const std::string &path,
		    const DownloadChunk &chunk
		);
		
	private:
		ChunkedDownloadConfig m_config;
		std::unique_ptr<CheckpointManager> m_checkpointManager;
	};
}
