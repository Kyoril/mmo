// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace mmo::updating
{
	/// Configuration for chunked downloads
	struct ChunkedDownloadConfig
	{
		/// Minimum file size (in bytes) to use chunked download
		std::uintmax_t minFileSizeForChunking = 5 * 1024 * 1024; // 5 MB
		
		/// Size of each chunk (in bytes)
		std::uintmax_t chunkSize = 1024 * 1024; // 1 MB
		
		/// Maximum number of concurrent chunk downloads
		unsigned maxConcurrentChunks = 4;
		
		/// Enable resume from checkpoint files
		bool enableResume = true;
		
		/// Directory to store checkpoint files
		std::string checkpointDir = ".update_checkpoints";
	};
	
	/// Represents a single download chunk
	struct DownloadChunk
	{
		std::uintmax_t start;
		std::uintmax_t end;
		std::uintmax_t size;
		bool completed = false;
		std::string checksum; // SHA1 of chunk data
	};
	
	/// Progress callback for chunk downloads
	/// Parameters: current bytes, total bytes, chunk index, total chunks
	using ChunkProgressCallback = std::function<void(
		std::uintmax_t, std::uintmax_t, size_t, size_t)>;
	
	/// Split a file size into chunks
	std::vector<DownloadChunk> createChunks(
		std::uintmax_t fileSize,
		std::uintmax_t chunkSize);
	
	/// Check if file should use chunked download
	bool shouldUseChunkedDownload(
		std::uintmax_t fileSize,
		const ChunkedDownloadConfig& config);
}
