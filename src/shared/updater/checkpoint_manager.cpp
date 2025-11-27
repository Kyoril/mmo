// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "checkpoint_manager.h"
#include "base/filesystem.h"
#include "simple_file_format/sff_write.h"
#include "simple_file_format/sff_read.h"
#include "simple_file_format/sff_read_tree.h"
#include "simple_file_format/sff_write_table.h"
#include <fstream>
#include <sstream>

namespace mmo::updating
{
	CheckpointManager::CheckpointManager(std::string checkpointDir)
		: m_checkpointDir(std::move(checkpointDir))
	{
		// Create checkpoint directory if it doesn't exist
		if (!std::filesystem::exists(m_checkpointDir))
		{
			std::filesystem::create_directories(m_checkpointDir);
		}
	}
	
	void CheckpointManager::saveCheckpoint(
		const std::string& filePath,
		const std::vector<DownloadChunk>& chunks)
	{
		const auto checkpointPath = getCheckpointPath(filePath);
		
		// Create parent directory if needed
		const auto parentPath = std::filesystem::path(checkpointPath).parent_path();
		if (!parentPath.empty() && !std::filesystem::exists(parentPath))
		{
			std::filesystem::create_directories(parentPath);
		}
		
		std::ofstream file(checkpointPath, std::ios::binary);
		if (!file)
		{
			throw std::runtime_error("Failed to create checkpoint file: " + checkpointPath);
		}
		
		// Write checkpoint in SFF format
		sff::write::Writer writer(file, sff::write::MultiLine);
		
		{
			writer.enterTable("checkpoint", sff::write::MultiLine);
			writer.Key("version") << sff::write::Identifier << "1";
			writer.Key("file") << filePath;
			writer.Key("chunks") << sff::write::Identifier << chunks.size();
			
			{
				writer.enterTable("chunk_list", sff::write::MultiLine);
				
				for (size_t i = 0; i < chunks.size(); ++i)
				{
					const auto& chunk = chunks[i];
					writer.enterTable("chunk", sff::write::MultiLine);
					writer.Key("index") << i;
					writer.Key("start") << chunk.start;
					writer.Key("end") << chunk.end;
					writer.Key("size") << chunk.size;
					writer.Key("completed") << (chunk.completed ? "true" : "false");
					if (!chunk.checksum.empty())
					{
						writer.Key("checksum") << chunk.checksum;
					}
					writer.leaveTable();
				}
				
				writer.leaveTable();
			}
			
			writer.leaveTable();
		}
	}
	
	std::optional<std::vector<DownloadChunk>> CheckpointManager::loadCheckpoint(
		const std::string& filePath)
	{
		const auto checkpointPath = getCheckpointPath(filePath);
		
		if (!std::filesystem::exists(checkpointPath))
		{
			return std::nullopt;
		}
		
		try
		{
			std::ifstream file(checkpointPath);
			if (!file)
			{
				return std::nullopt;
			}
			
			auto checkpoint = sff::read::tree::Table::readTable(file, checkpointPath);
			
			std::vector<DownloadChunk> chunks;
			
			if (const auto* chunkList = checkpoint.tryGetTable("chunk_list"))
			{
				for (const auto& entry : chunkList->getEntries())
				{
					if (const auto* chunkTable = entry.tryGetTable())
					{
						DownloadChunk chunk;
						chunkTable->tryGetInteger("start", chunk.start);
						chunkTable->tryGetInteger("end", chunk.end);
						chunkTable->tryGetInteger("size", chunk.size);
						
						std::string completedStr;
						if (chunkTable->tryGetString("completed", completedStr))
						{
							chunk.completed = (completedStr == "true");
						}
						
						chunkTable->tryGetString("checksum", chunk.checksum);
						
						chunks.push_back(chunk);
					}
				}
			}
			
			return chunks;
		}
		catch (...)
		{
			// If checkpoint file is corrupted, ignore it
			return std::nullopt;
		}
	}
	
	void CheckpointManager::removeCheckpoint(const std::string& filePath)
	{
		const auto checkpointPath = getCheckpointPath(filePath);
		
		if (std::filesystem::exists(checkpointPath))
		{
			std::filesystem::remove(checkpointPath);
		}
	}
	
	void CheckpointManager::clearAllCheckpoints()
	{
		if (std::filesystem::exists(m_checkpointDir))
		{
			std::filesystem::remove_all(m_checkpointDir);
			std::filesystem::create_directories(m_checkpointDir);
		}
	}
	
	std::string CheckpointManager::getCheckpointPath(const std::string& filePath) const
	{
		// Convert file path to checkpoint filename (replace slashes with underscores)
		std::string safeName = filePath;
		std::replace(safeName.begin(), safeName.end(), '/', '_');
		std::replace(safeName.begin(), safeName.end(), '\\', '_');
		std::replace(safeName.begin(), safeName.end(), ':', '_');
		
		return (std::filesystem::path(m_checkpointDir) / (safeName + ".checkpoint")).string();
	}
}
