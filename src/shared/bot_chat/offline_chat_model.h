// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "chat_model.h"

#include <deque>
#include <iterator>
#include <random>

namespace mmo
{
	/// Answers with canned lines, without a network or a bill. For exercising the chat plumbing
	/// of a swarm - who answers, when, through which channel - before spending anything on it.
	class OfflineChatModel final : public IChatModel
	{
	public:
		explicit OfflineChatModel(const uint32 seed) : m_rng(seed) {}

		[[nodiscard]] bool Submit(ChatCompletionRequest request) override
		{
			static const char* const lines[] = {
				"lol yeah", "anyone wanna group?", "where do i find the trainer", "brb",
				"these wolves hit hard", "nice", "ty", "idk tbh", "ding!", "same lol",
			};

			ChatCompletionResult result;
			result.ticket = request.ticket;
			result.succeeded = true;
			result.text = lines[std::uniform_int_distribution<std::size_t>(0, std::size(lines) - 1)(m_rng)];
			m_results.push_back(std::move(result));
			return true;
		}

		void Poll(std::vector<ChatCompletionResult>& outResults) override
		{
			while (!m_results.empty())
			{
				outResults.push_back(std::move(m_results.front()));
				m_results.pop_front();
			}
		}

		[[nodiscard]] std::size_t GetPendingCount() const override { return m_results.size(); }

		void Stop() override { m_results.clear(); }

	private:
		std::mt19937 m_rng;
		std::deque<ChatCompletionResult> m_results;
	};
}
