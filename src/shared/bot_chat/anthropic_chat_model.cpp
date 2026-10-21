// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "anthropic_chat_model.h"

#include "https_client/send_request.h"

#include "nlohmann/json.hpp"

#include <chrono>
#include <exception>
#include <iterator>
#include <memory>
#include <utility>

namespace mmo
{
	namespace
	{
		constexpr const char* ApiHost = "api.anthropic.com";
		constexpr uint16 ApiPort = 443;
		constexpr const char* ApiVersion = "2023-06-01";

		/// A chat line is a few dozen tokens; anything near this is not an answer worth reading.
		constexpr std::uintmax_t MaxResponseBytes = 64 * 1024;

		constexpr std::size_t MaxErrorLength = 300;
	}

	AnthropicChatModel::AnthropicChatModel(std::string apiKey, std::string model, const std::size_t maxQueued)
		: m_apiKey(std::move(apiKey))
		, m_model(std::move(model))
		, m_maxQueued(maxQueued)
	{
		m_worker = std::thread([this]() { WorkerMain(); });
	}

	AnthropicChatModel::~AnthropicChatModel()
	{
		Stop();
	}

	bool AnthropicChatModel::Submit(ChatCompletionRequest request)
	{
		{
			std::scoped_lock lock(m_mutex);
			if (m_stopping || m_queue.size() >= m_maxQueued)
			{
				return false;
			}

			m_queue.push_back(std::move(request));
		}

		m_wake.notify_one();
		return true;
	}

	void AnthropicChatModel::Poll(std::vector<ChatCompletionResult>& outResults)
	{
		std::scoped_lock lock(m_mutex);
		while (!m_results.empty())
		{
			outResults.push_back(std::move(m_results.front()));
			m_results.pop_front();
		}
	}

	std::size_t AnthropicChatModel::GetPendingCount() const
	{
		std::scoped_lock lock(m_mutex);
		return m_queue.size() + m_inFlight + m_results.size();
	}

	void AnthropicChatModel::Stop()
	{
		{
			std::scoped_lock lock(m_mutex);
			m_stopping = true;
			m_queue.clear();
		}

		m_cancel = true;
		m_wake.notify_all();

		if (m_worker.joinable())
		{
			m_worker.join();
		}
	}

	std::string AnthropicChatModel::BuildRequestBody(const std::string& model, const ChatCompletionRequest& request)
	{
		const nlohmann::json body = {
			{ "model", model },
			{ "max_tokens", request.maxOutputTokens },
			{ "system", request.system },
			{ "messages", nlohmann::json::array({
				{ { "role", "user" }, { "content", request.prompt } } }) },
			// Thinking tokens are billed as output. A one-line chat reply has nothing to think
			// about, and leaving it on would multiply the cost of every request.
			{ "thinking", { { "type", "disabled" } } },
			{ "output_config", { { "effort", "low" } } },
		};

		return body.dump();
	}

	ChatCompletionResult AnthropicChatModel::ParseResponse(const uint64 ticket, const unsigned status, const std::string& body)
	{
		ChatCompletionResult result;
		result.ticket = ticket;

		const nlohmann::json data = nlohmann::json::parse(body, nullptr, false);

		if (status != 200)
		{
			// Error responses are not billed.
			result.error = "HTTP " + std::to_string(status);
			if (!data.is_discarded() && data.contains("error") && data["error"].is_object())
			{
				result.error += ": " + data["error"].value("message", std::string());
			}

			result.error = result.error.substr(0, MaxErrorLength);
			return result;
		}

		result.reachedModel = true;

		if (data.is_discarded() || !data.is_object())
		{
			result.error = "unreadable response";
			return result;
		}

		if (data.contains("usage") && data["usage"].is_object())
		{
			const nlohmann::json& usage = data["usage"];
			result.inputTokens = usage.value("input_tokens", 0u)
				+ usage.value("cache_creation_input_tokens", 0u)
				+ usage.value("cache_read_input_tokens", 0u);
			result.outputTokens = usage.value("output_tokens", 0u);
		}

		const std::string stopReason = data.value("stop_reason", std::string());
		if (stopReason == "refusal")
		{
			result.error = "refusal";
			return result;
		}

		if (data.contains("content") && data["content"].is_array())
		{
			for (const nlohmann::json& block : data["content"])
			{
				if (block.is_object() && block.value("type", std::string()) == "text")
				{
					result.text += block.value("text", std::string());
				}
			}
		}

		result.succeeded = !result.text.empty();
		if (!result.succeeded)
		{
			result.error = "empty response (" + stopReason + ")";
		}

		return result;
	}

	void AnthropicChatModel::WorkerMain()
	{
		for (;;)
		{
			ChatCompletionRequest request;
			{
				std::unique_lock lock(m_mutex);
				m_wake.wait(lock, [this]() { return m_stopping || !m_queue.empty(); });
				if (m_stopping)
				{
					return;
				}

				request = std::move(m_queue.front());
				m_queue.pop_front();
				++m_inFlight;
			}

			ChatCompletionResult result = Execute(request);

			std::scoped_lock lock(m_mutex);
			--m_inFlight;
			if (!m_stopping)
			{
				m_results.push_back(std::move(result));
			}
		}
	}

	ChatCompletionResult AnthropicChatModel::Execute(const ChatCompletionRequest& request)
	{
		net::https_client::Request httpRequest;
		httpRequest.host = ApiHost;
		httpRequest.document = "/v1/messages";
		httpRequest.method = "POST";
		httpRequest.headers = {
			{ "content-type", "application/json" },
			{ "x-api-key", m_apiKey },
			{ "anthropic-version", ApiVersion },
		};
		httpRequest.body = BuildRequestBody(m_model, request);

		net::https_client::RequestOptions options;
		options.inactivityTimeout = std::chrono::seconds(20);
		options.cancel = &m_cancel;
		options.maxResponseBytes = MaxResponseBytes;

		try
		{
			const net::https_client::Response response = net::https_client::sendRequest(ApiHost, ApiPort, httpRequest, options);

			std::string body;
			if (response.body)
			{
				body.assign(std::istreambuf_iterator<char>(*response.body), std::istreambuf_iterator<char>());
			}

			return ParseResponse(request.ticket, response.status, body);
		}
		catch (const std::exception& ex)
		{
			// The request may have reached the model before the connection failed, so it is
			// treated as billed: its reservation is kept rather than refunded.
			ChatCompletionResult result;
			result.ticket = request.ticket;
			result.reachedModel = true;
			result.error = std::string("transport: ") + ex.what();
			result.error = result.error.substr(0, MaxErrorLength);
			return result;
		}
	}
}
