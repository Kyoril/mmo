// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "updater/retrying_update_source.h"
#include "updater/update_errors.h"

#include <atomic>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace mmo;
using namespace mmo::updating;

namespace
{
	/// Fails a configurable number of times before serving `content`.
	struct FlakySource final : IUpdateSource
	{
		explicit FlakySource(const uint32 failures, std::string content = "payload")
			: m_failures(failures)
			, m_content(std::move(content))
		{
		}

		UpdateSourceFile readFile(const std::string& path) override
		{
			++calls;
			if (calls <= m_failures)
			{
				throw std::runtime_error(path + ": connection reset");
			}

			return UpdateSourceFile(std::any(), std::make_unique<std::istringstream>(m_content), m_content.size());
		}

		uint32 calls = 0;

	private:
		uint32 m_failures;
		std::string m_content;
	};

	struct PermanentFailureSource final : IUpdateSource
	{
		UpdateSourceFile readFile(const std::string& path) override
		{
			++calls;
			throw PermanentSourceError(path + ": HTTP response 404");
		}

		uint32 calls = 0;
	};

	RetryPolicy MakeFastPolicy(const uint32 maxAttempts)
	{
		RetryPolicy policy;
		policy.maxAttempts = maxAttempts;
		policy.initialDelay = std::chrono::milliseconds(0);
		policy.maxDelay = std::chrono::milliseconds(0);
		return policy;
	}

	std::string ReadAll(UpdateSourceFile& file)
	{
		std::ostringstream out;
		out << file.content->rdbuf();
		return out.str();
	}
}

TEST_CASE("GetRetryDelay doubles up to the maximum", "[updater][retry]")
{
	RetryPolicy policy;
	policy.initialDelay = std::chrono::milliseconds(1000);
	policy.maxDelay = std::chrono::milliseconds(5000);

	CHECK(GetRetryDelay(policy, 1) == std::chrono::milliseconds(1000));
	CHECK(GetRetryDelay(policy, 2) == std::chrono::milliseconds(2000));
	CHECK(GetRetryDelay(policy, 3) == std::chrono::milliseconds(4000));
	CHECK(GetRetryDelay(policy, 4) == std::chrono::milliseconds(5000));
	CHECK(GetRetryDelay(policy, 100) == std::chrono::milliseconds(5000));
}

TEST_CASE("RetryingUpdateSource passes a working read straight through", "[updater][retry]")
{
	auto inner = std::make_unique<FlakySource>(0);
	FlakySource& flaky = *inner;

	RetryingUpdateSource source(std::move(inner), MakeFastPolicy(3), nullptr);
	bool retried = false;
	bool recovered = false;
	source.onRetry = [&](const std::string&, uint32, uint32, const std::string&) { retried = true; };
	source.onRecovered = [&](const std::string&) { recovered = true; };

	auto file = source.readFile("data/file.bin");

	CHECK(ReadAll(file) == "payload");
	CHECK(flaky.calls == 1);
	CHECK_FALSE(retried);
	CHECK_FALSE(recovered);
}

TEST_CASE("RetryingUpdateSource recovers from temporary failures", "[updater][retry]")
{
	auto inner = std::make_unique<FlakySource>(2);
	FlakySource& flaky = *inner;

	RetryingUpdateSource source(std::move(inner), MakeFastPolicy(5), nullptr);

	std::vector<uint32> retriedAttempts;
	std::string recoveredPath;
	source.onRetry = [&](const std::string& path, const uint32 attempt, const uint32 maxAttempts, const std::string& error)
	{
		CHECK(path == "data/file.bin");
		CHECK(maxAttempts == 5);
		CHECK(error.find("connection reset") != std::string::npos);
		retriedAttempts.push_back(attempt);
	};
	source.onRecovered = [&](const std::string& path) { recoveredPath = path; };

	auto file = source.readFile("data/file.bin");

	CHECK(ReadAll(file) == "payload");
	CHECK(flaky.calls == 3);
	CHECK(retriedAttempts == std::vector<uint32>{ 1, 2 });
	CHECK(recoveredPath == "data/file.bin");
}

TEST_CASE("RetryingUpdateSource gives up after the last attempt", "[updater][retry]")
{
	auto inner = std::make_unique<FlakySource>(100);
	FlakySource& flaky = *inner;

	RetryingUpdateSource source(std::move(inner), MakeFastPolicy(4), nullptr);

	uint32 retries = 0;
	source.onRetry = [&](const std::string&, uint32, uint32, const std::string&) { ++retries; };

	CHECK_THROWS_AS(source.readFile("data/file.bin"), SourceUnavailableError);
	CHECK(flaky.calls == 4);
	CHECK(retries == 3);
}

TEST_CASE("RetryingUpdateSource does not retry permanent errors", "[updater][retry]")
{
	auto inner = std::make_unique<PermanentFailureSource>();
	PermanentFailureSource& failing = *inner;

	RetryingUpdateSource source(std::move(inner), MakeFastPolicy(5), nullptr);

	CHECK_THROWS_AS(source.readFile("missing.bin"), PermanentSourceError);
	CHECK(failing.calls == 1);
}

TEST_CASE("RetryingUpdateSource stops waiting when cancelled", "[updater][retry]")
{
	auto inner = std::make_unique<FlakySource>(100);

	RetryPolicy policy;
	policy.maxAttempts = 10;
	policy.initialDelay = std::chrono::milliseconds(60000);
	policy.maxDelay = std::chrono::milliseconds(60000);

	std::atomic<bool> cancel{ false };
	RetryingUpdateSource source(std::move(inner), policy, &cancel);

	// Cancel once the first attempt has failed and the source is waiting a full minute.
	source.onRetry = [&](const std::string&, uint32, uint32, const std::string&)
	{
		std::thread([&cancel]
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			cancel = true;
		}).detach();
	};

	const auto startedAt = std::chrono::steady_clock::now();
	CHECK_THROWS_AS(source.readFile("data/file.bin"), UpdateCancelled);
	CHECK(std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(5));
}

TEST_CASE("RetryingUpdateSource does not start reading when already cancelled", "[updater][retry]")
{
	auto inner = std::make_unique<FlakySource>(0);
	FlakySource& flaky = *inner;

	std::atomic<bool> cancel{ true };
	RetryingUpdateSource source(std::move(inner), MakeFastPolicy(3), &cancel);

	CHECK_THROWS_AS(source.readFile("data/file.bin"), UpdateCancelled);
	CHECK(flaky.calls == 0);
}
