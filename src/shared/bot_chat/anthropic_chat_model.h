// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "chat_model.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace mmo
{
	/// Calls the Claude Messages API over HTTPS on a worker thread.
	///
	/// One worker, one request at a time: chat lines are small and rare, and serializing them is
	/// the simplest way to keep a misbehaving budget from turning into a burst of parallel spend.
	/// The worker never logs and never touches anything but its own queues, so it is safe in a
	/// host whose log signal is main-thread only.
	class AnthropicChatModel final : public IChatModel
	{
	public:
		/// @param apiKey Anthropic API key. Never logged and never placed in a prompt.
		/// @param model Model id, e.g. "claude-haiku-5-5".
		/// @param maxQueued Requests that may wait for the worker before Submit refuses more.
		AnthropicChatModel(std::string apiKey, std::string model, std::size_t maxQueued = 4);
		~AnthropicChatModel() override;

		[[nodiscard]] bool Submit(ChatCompletionRequest request) override;
		void Poll(std::vector<ChatCompletionResult>& outResults) override;
		[[nodiscard]] std::size_t GetPendingCount() const override;
		void Stop() override;

		/// Builds the JSON body of a Messages API request. Public for tests.
		[[nodiscard]] static std::string BuildRequestBody(const std::string& model, const ChatCompletionRequest& request);

		/// Reads a Messages API response into a result. Public for tests.
		/// @param status HTTP status code.
		/// @param body Response body.
		[[nodiscard]] static ChatCompletionResult ParseResponse(uint64 ticket, unsigned status, const std::string& body);

	private:
		void WorkerMain();
		[[nodiscard]] ChatCompletionResult Execute(const ChatCompletionRequest& request);

	private:
		std::string m_apiKey;
		std::string m_model;
		std::size_t m_maxQueued;

		mutable std::mutex m_mutex;
		std::condition_variable m_wake;
		std::deque<ChatCompletionRequest> m_queue;
		std::deque<ChatCompletionResult> m_results;
		std::size_t m_inFlight { 0 };
		bool m_stopping { false };
		std::atomic<bool> m_cancel { false };
		std::thread m_worker;
	};
}
