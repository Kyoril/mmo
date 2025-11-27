// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "update_source.h"
#include "retry_logic.h"
#include <memory>
#include <functional>

namespace mmo::updating
{
	/// Wrapper around IUpdateSource that adds retry logic
	class RetryUpdateSource : public IUpdateSource
	{
	public:
		/// Progress callback for retry attempts
		/// Parameters: file path, attempt number, error message, retry delay
		using RetryProgressCallback = std::function<void(
			const std::string&, unsigned, const std::string&, std::chrono::milliseconds)>;
		
		explicit RetryUpdateSource(
			std::unique_ptr<IUpdateSource> innerSource,
			RetryConfig config = RetryConfig{},
			RetryProgressCallback progressCallback = nullptr
		);
		
		UpdateSourceFile readFile(const std::string& path) override;
		
	private:
		std::unique_ptr<IUpdateSource> m_innerSource;
		RetryConfig m_config;
		RetryProgressCallback m_progressCallback;
	};
}
