// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <cstdint>
#include <chrono>
#include <functional>
#include <thread>
#include <random>
#include <stdexcept>
#include <algorithm>

namespace mmo::updating
{
	/// Configuration for retry behavior
	struct RetryConfig
	{
		/// Maximum number of retry attempts (0 = no retries)
		unsigned maxRetries = 3;
		
		/// Initial delay before first retry (in milliseconds)
		std::chrono::milliseconds initialDelay{1000};
		
		/// Maximum delay between retries (in milliseconds)
		std::chrono::milliseconds maxDelay{30000};
		
		/// Multiplier for exponential backoff (e.g., 2.0 doubles the delay each time)
		double backoffMultiplier = 2.0;
		
		/// Whether to add random jitter to retry delays
		bool useJitter = true;
		
		/// Jitter factor (0.0 to 1.0) - adds up to this fraction of delay as random variation
		double jitterFactor = 0.1;
	};
	
	namespace detail
	{
		inline std::chrono::milliseconds calculateDelay(
			unsigned attemptNumber,
			const RetryConfig& config)
		{
			// Calculate exponential backoff
			auto delay = config.initialDelay;
			for (unsigned i = 0; i < attemptNumber; ++i)
			{
				delay = std::chrono::milliseconds(
					static_cast<long long>(delay.count() * config.backoffMultiplier)
				);
			}
			
			// Cap at maximum delay
			delay = std::min(delay, config.maxDelay);
			
			// Add jitter if enabled
			if (config.useJitter && config.jitterFactor > 0.0)
			{
				static std::random_device rd;
				static std::mt19937 gen(rd());
				std::uniform_real_distribution<> dis(0.0, config.jitterFactor);
				
				auto jitter = static_cast<long long>(delay.count() * dis(gen));
				delay += std::chrono::milliseconds(jitter);
			}
			
			return delay;
		}
	}
	
	/// Retry a function with exponential backoff
	/// @tparam Func Function type that returns a result
	/// @param func Function to execute
	/// @param config Retry configuration
	/// @param errorCallback Optional callback invoked on each retry attempt (attempt number, error message, delay)
	/// @return Result of the function
	/// @throws Last exception if all retries fail
	template<typename Func>
	auto retryWithBackoff(
		Func&& func,
		const RetryConfig& config = RetryConfig{},
		std::function<void(unsigned, const std::string&, std::chrono::milliseconds)> errorCallback = nullptr
	) -> decltype(func())
	{
		std::exception_ptr lastException;
		
		for (unsigned attempt = 0; attempt <= config.maxRetries; ++attempt)
		{
			try
			{
				return func();
			}
			catch (const std::exception& ex)
			{
				lastException = std::current_exception();
				
				// If this was the last attempt, rethrow
				if (attempt >= config.maxRetries)
				{
					std::rethrow_exception(lastException);
				}
				
				// Calculate delay for next retry
				auto delay = detail::calculateDelay(attempt, config);
				
				// Invoke error callback if provided
				if (errorCallback)
				{
					errorCallback(attempt + 1, ex.what(), delay);
				}
				
				// Wait before retrying
				std::this_thread::sleep_for(delay);
			}
			catch (...)
			{
				lastException = std::current_exception();
				
				// If this was the last attempt, rethrow
				if (attempt >= config.maxRetries)
				{
					std::rethrow_exception(lastException);
				}
				
				// Calculate delay for next retry
				auto delay = detail::calculateDelay(attempt, config);
				
				// Invoke error callback if provided
				if (errorCallback)
				{
					errorCallback(attempt + 1, "Unknown error", delay);
				}
				
				// Wait before retrying
				std::this_thread::sleep_for(delay);
			}
		}
		
		// Should never reach here, but rethrow last exception if we do
		std::rethrow_exception(lastException);
	}
}
