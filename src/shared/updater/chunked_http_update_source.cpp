// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chunked_http_update_source.h"
#include "virtual_dir/path.h"
#include "http_client/send_request.h"
#include "base/sha1.h"
#include <sstream>
#include <fstream>
#include <thread>
#include <mutex>
#include <algorithm>
#include "asio.hpp"

namespace mmo::updating
{
	ChunkedHTTPUpdateSource::ChunkedHTTPUpdateSource(
	    std::string host,
	    uint16 port,
	    std::string path,
	    ChunkedDownloadConfig config
	)
		: HTTPUpdateSource(std::move(host), port, std::move(path))
		, m_config(std::move(config))
	{
		if (m_config.enableResume)
		{
			m_checkpointManager = std::make_unique<CheckpointManager>(m_config.checkpointDir);
		}
	}

	UpdateSourceFile ChunkedHTTPUpdateSource::readFile(
	    const std::string &path
	)
	{
		// For now, use base class implementation
		// This will be enhanced to auto-detect file size and use chunked download
		return HTTPUpdateSource::readFile(path);
	}
	
	UpdateSourceFile ChunkedHTTPUpdateSource::readFileChunked(
	    const std::string &path,
	    std::uintmax_t fileSize,
	    ChunkProgressCallback progressCallback
	)
	{
		// Check if file should use chunked download
		if (!shouldUseChunkedDownload(fileSize, m_config))
		{
			return HTTPUpdateSource::readFile(path);
		}
		
		// Create or load chunks
		auto chunks = createChunks(fileSize, m_config.chunkSize);
		
		// Try to load checkpoint if resume is enabled
		if (m_config.enableResume && m_checkpointManager)
		{
			auto savedChunks = m_checkpointManager->loadCheckpoint(path);
			if (savedChunks && savedChunks->size() == chunks.size())
			{
				// Use saved progress
				chunks = std::move(*savedChunks);
			}
		}
		
		// Prepare buffer for complete file
		std::vector<char> fileBuffer(fileSize);
		std::mutex bufferMutex;
		std::atomic<std::uintmax_t> totalDownloaded{0};
		std::atomic<size_t> chunksCompleted{0};
		
		// Create ASIO dispatcher for parallel chunk downloads
		asio::io_service dispatcher;
		
		// Queue incomplete chunks for download
		for (size_t i = 0; i < chunks.size(); ++i)
		{
			if (chunks[i].completed)
			{
				chunksCompleted++;
				totalDownloaded += chunks[i].size;
				continue;
			}
			
			dispatcher.post([&, i]()
			{
				try
				{
					auto& chunk = chunks[i];
					auto chunkData = downloadChunk(path, chunk);
					
					// Copy chunk data to file buffer
					{
						std::lock_guard<std::mutex> lock(bufferMutex);
						std::copy(
							chunkData.begin(),
							chunkData.end(),
							fileBuffer.begin() + chunk.start);
					}
					
					// Mark chunk as completed
					chunk.completed = true;
					totalDownloaded += chunk.size;
					chunksCompleted++;
					
					// Save checkpoint
					if (m_config.enableResume && m_checkpointManager)
					{
						std::lock_guard<std::mutex> lock(bufferMutex);
						m_checkpointManager->saveCheckpoint(path, chunks);
					}
					
					// Report progress
					if (progressCallback)
					{
						progressCallback(
							totalDownloaded.load(),
							fileSize,
							chunksCompleted.load(),
							chunks.size());
					}
				}
				catch (const std::exception& ex)
				{
					// Log error but continue with other chunks
					// TODO: Add proper error handling
					throw;
				}
			});
		}
		
		// Create worker threads
		std::vector<std::thread> threads;
		for (unsigned i = 0; i < m_config.maxConcurrentChunks; ++i)
		{
			threads.emplace_back([&dispatcher]()
			{
				dispatcher.run();
			});
		}
		
		// Wait for all chunks to complete
		for (auto& thread : threads)
		{
			thread.join();
		}
		
		// Verify all chunks completed
		if (chunksCompleted != chunks.size())
		{
			throw std::runtime_error(
				path + ": Failed to download all chunks (" +
				std::to_string(chunksCompleted.load()) + "/" +
				std::to_string(chunks.size()) + ")");
		}
		
		// Remove checkpoint on success
		if (m_config.enableResume && m_checkpointManager)
		{
			m_checkpointManager->removeCheckpoint(path);
		}
		
		// Create stream from buffer
		auto bufferPtr = std::make_shared<std::vector<char>>(std::move(fileBuffer));
		auto stream = std::make_unique<std::istream>(
			new std::basic_streambuf<char>());
		
		// Use stringstream for now (TODO: optimize with custom streambuf)
		auto stringStream = std::make_unique<std::stringstream>();
		stringStream->write(bufferPtr->data(), bufferPtr->size());
		stringStream->seekg(0);
		
		return UpdateSourceFile(
		    bufferPtr,
		    std::move(stringStream),
		    fileSize
		);
	}
	
	std::vector<char> ChunkedHTTPUpdateSource::downloadChunk(
	    const std::string &path,
	    const DownloadChunk &chunk
	)
	{
		net::http_client::Request request;
		request.host = m_host;
		request.document = m_path;
		virtual_dir::appendPath(request.document, path);
		request.byteRange = std::make_pair(chunk.start, chunk.end);

		auto response = net::http_client::sendRequest(
		                    m_host,
		                    m_port,
		                    request);

		if (response.status != net::http_client::Response::Ok &&
		    response.status != net::http_client::Response::PartialContent)
		{
			throw std::runtime_error(
			    path + " chunk [" + std::to_string(chunk.start) + "-" +
			    std::to_string(chunk.end) + "]: HTTP response " +
			    std::to_string(response.status));
		}
		
		// Read chunk data
		std::vector<char> chunkData(chunk.size);
		response.body->read(chunkData.data(), chunk.size);
		
		if (response.body->gcount() != static_cast<std::streamsize>(chunk.size))
		{
			throw std::runtime_error(
			    path + " chunk [" + std::to_string(chunk.start) + "-" +
			    std::to_string(chunk.end) + "]: Expected " +
			    std::to_string(chunk.size) + " bytes but got " +
			    std::to_string(response.body->gcount()));
		}
		
		return chunkData;
	}
}
