// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <string>
#include <vector>

namespace mmo
{
	/// One request for a single line of chat.
	struct ChatCompletionRequest final
	{
		/// Caller's id for the request; handed back with the result.
		uint64 ticket { 0 };

		std::string system;
		std::string prompt;
		uint32 maxOutputTokens { 80 };
	};

	struct ChatCompletionResult final
	{
		uint64 ticket { 0 };

		/// The model produced text.
		bool succeeded { false };

		/// Whether the request reached the model and may have been billed. A request that failed
		/// on the way there (no connection, rejected with an error status) was not; one that
		/// failed after it was sent may have been, and is charged in full.
		bool reachedModel { false };

		uint32 inputTokens { 0 };
		uint32 outputTokens { 0 };
		std::string text;
		std::string error;
	};

	/// A language model that turns a prompt into a line of chat, asynchronously.
	///
	/// Submit and Poll are called from the host thread only. Implementations do their network
	/// work elsewhere so that a slow answer never stalls the frame every bot shares.
	class IChatModel : public NonCopyable
	{
	public:
		~IChatModel() override = default;

		/// Queues a request.
		/// @return False if the model is not accepting requests right now (queue full or stopped).
		[[nodiscard]] virtual bool Submit(ChatCompletionRequest request) = 0;

		/// Moves every finished result into outResults.
		virtual void Poll(std::vector<ChatCompletionResult>& outResults) = 0;

		/// Requests submitted whose result was not polled yet.
		[[nodiscard]] virtual std::size_t GetPendingCount() const = 0;

		/// Stops accepting requests, abandons the ones in flight and joins any worker.
		virtual void Stop() = 0;
	};
}
