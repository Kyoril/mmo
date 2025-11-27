// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "chunked_download.h"
#include <string>
#include <vector>
#include <optional>

namespace mmo::updating
{
	/// Manages download checkpoints for resume capability
	class CheckpointManager
	{
	public:
		explicit CheckpointManager(std::string checkpointDir);
		
		/// Save checkpoint for a file download
		void saveCheckpoint(
			const std::string& filePath,
			const std::vector<DownloadChunk>& chunks);
		
		/// Load checkpoint for a file download
		std::optional<std::vector<DownloadChunk>> loadCheckpoint(
			const std::string& filePath);
		
		/// Remove checkpoint file after successful download
		void removeCheckpoint(const std::string& filePath);
		
		/// Clear all checkpoint files
		void clearAllCheckpoints();
		
	private:
		std::string m_checkpointDir;
		
		/// Get checkpoint file path for a given file
		std::string getCheckpointPath(const std::string& filePath) const;
	};
}
