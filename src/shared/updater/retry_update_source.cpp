// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "retry_update_source.h"
#include "retry_logic.h"

namespace mmo::updating
{
	RetryUpdateSource::RetryUpdateSource(
		std::unique_ptr<IUpdateSource> innerSource,
		RetryConfig config,
		RetryProgressCallback progressCallback
	)
		: m_innerSource(std::move(innerSource))
		, m_config(std::move(config))
		, m_progressCallback(std::move(progressCallback))
	{
	}
	
	UpdateSourceFile RetryUpdateSource::readFile(const std::string& path)
	{
		// Create retry callback that includes the file path
		auto errorCallback = [this, &path](
			unsigned attempt,
			const std::string& error,
			std::chrono::milliseconds delay)
		{
			if (m_progressCallback)
			{
				m_progressCallback(path, attempt, error, delay);
			}
		};
		
		// Wrap the readFile call with retry logic
		return retryWithBackoff<std::function<UpdateSourceFile()>>(
			[this, &path]() -> UpdateSourceFile
			{
				return m_innerSource->readFile(path);
			},
			m_config,
			errorCallback
		);
	}
}
