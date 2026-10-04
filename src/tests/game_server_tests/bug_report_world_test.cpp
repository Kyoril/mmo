// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "world_server/bug_report_document.h"
#include "world_server/bug_report_health.h"
#include "world_server/bug_report_transport.h"
#include "world_server/bug_report_uploader.h"
#include "world_server/combat_event_log.h"
#include "world_server/subsystem_state.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace mmo;
using json = nlohmann::json;

// ---------------------------------------------------------------------------------------------
// WorldSubsystemState
// ---------------------------------------------------------------------------------------------

TEST_CASE("WorldSubsystemState is available only when config, operator and health agree", "[bug_report]")
{
	WorldSubsystemState state;
	std::vector<WorldSubsystemState::StatusList> changes;
	auto connection = state.statusChanged.connect([&changes](const WorldSubsystemState::StatusList& list) { changes.push_back(list); });

	CHECK_FALSE(state.IsAvailable(game::subsystem::BugReport));

	state.SetConfigEnabled(game::subsystem::BugReport, true);
	CHECK(state.IsAvailable(game::subsystem::BugReport));
	REQUIRE(changes.size() == 1);
	CHECK(changes[0][0].second == game::subsystem_status::Available);

	state.SetManualEnabled(game::subsystem::BugReport, false);
	CHECK_FALSE(state.IsAvailable(game::subsystem::BugReport));

	// Recovering health must not override the operator's "off".
	state.SetHealthy(game::subsystem::BugReport, false);
	state.SetHealthy(game::subsystem::BugReport, true);
	CHECK_FALSE(state.IsAvailable(game::subsystem::BugReport));

	state.SetManualEnabled(game::subsystem::BugReport, true);
	CHECK(state.IsAvailable(game::subsystem::BugReport));

	// Two changes of the effective status since the first: off (manual), on (manual).
	CHECK(changes.size() == 3);
}

TEST_CASE("WorldSubsystemState only signals effective changes", "[bug_report]")
{
	WorldSubsystemState state;
	int changes = 0;
	auto connection = state.statusChanged.connect([&changes](const WorldSubsystemState::StatusList&) { ++changes; });

	state.SetHealthy(game::subsystem::BugReport, false);
	state.SetHealthy(game::subsystem::BugReport, true);
	CHECK(changes == 0);

	const auto full = state.GetWorldOwnedStatus();
	REQUIRE(full.size() == 1);
	CHECK(full[0].first == game::subsystem::BugReport);
	CHECK(full[0].second == game::subsystem_status::Unavailable);
}

// ---------------------------------------------------------------------------------------------
// BugReportHealth
// ---------------------------------------------------------------------------------------------

TEST_CASE("BugReportHealth trips after consecutive failures and recovers after the cooldown", "[bug_report]")
{
	BugReportHealth health(3, 60000);
	health.OnAttemptFailed(1000);
	health.OnAttemptFailed(2000);
	CHECK(health.IsHealthy());
	health.OnAttemptFailed(3000);
	CHECK_FALSE(health.IsHealthy());

	health.Update(62999);
	CHECK_FALSE(health.IsHealthy());
	health.Update(63000);
	CHECK(health.IsHealthy());

	// The failure count was reset with the recovery.
	health.OnAttemptFailed(64000);
	CHECK(health.IsHealthy());
}

TEST_CASE("BugReportHealth closes immediately on delivery and trips on a full queue", "[bug_report]")
{
	BugReportHealth health(3, 60000);
	health.OnQueueFull(100);
	CHECK_FALSE(health.IsHealthy());
	health.OnDelivered();
	CHECK(health.IsHealthy());

	health.OnAttemptFailed(1);
	health.OnAttemptFailed(2);
	health.OnDelivered();
	health.OnAttemptFailed(3);
	CHECK(health.IsHealthy());
}

// ---------------------------------------------------------------------------------------------
// CombatEventLog
// ---------------------------------------------------------------------------------------------

TEST_CASE("CombatEventLog keeps the newest events up to its capacity", "[bug_report]")
{
	CombatEventLog log(3);
	for (uint32 i = 1; i <= 5; ++i)
	{
		CombatEvent event;
		event.time = i;
		event.amount = i;
		log.Add(event);
	}

	const auto& events = log.GetEvents();
	REQUIRE(events.size() == 3);
	CHECK(events.front().amount == 3);
	CHECK(events.back().amount == 5);
}

TEST_CASE("CombatEventLog finds the last cast result of a spell", "[bug_report]")
{
	CombatEventLog log;
	log.Add({ 1, combat_event_type::CastFinished, 0, 7, 1, 0 });
	log.Add({ 2, combat_event_type::CastFinished, 0, 8, 1, 0 });
	log.Add({ 3, combat_event_type::CastFinished, 0, 7, 0, 0 });

	const CombatEvent* last = log.FindLastCastResult(7);
	REQUIRE(last != nullptr);
	CHECK(last->time == 3);
	CHECK(last->amount == 0);
	CHECK(log.FindLastCastResult(9) == nullptr);
}

// ---------------------------------------------------------------------------------------------
// BugReportUploader
// ---------------------------------------------------------------------------------------------

namespace
{
	/// Collects posted work and runs it on the test thread, like the io_service would.
	struct PostQueue
	{
		std::mutex mutex;
		std::vector<std::function<void()>> work;

		BugReportUploader::Poster Poster()
		{
			return [this](std::function<void()> fn)
			{
				std::scoped_lock lock{ mutex };
				work.push_back(std::move(fn));
			};
		}

		void Drain()
		{
			std::vector<std::function<void()>> pending;
			{
				std::scoped_lock lock{ mutex };
				pending.swap(work);
			}
			for (auto& fn : pending)
			{
				fn();
			}
		}
	};

	template<class Predicate>
	bool WaitFor(PostQueue& queue, Predicate&& done)
	{
		for (int i = 0; i < 500; ++i)
		{
			queue.Drain();
			if (done())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}
}

TEST_CASE("BugReportUploader delivers a report", "[bug_report]")
{
	PostQueue queue;
	std::vector<BugReportUploader::Outcome> outcomes;
	std::atomic<int> calls{ 0 };

	BugReportUploader uploader(
		[&calls](const std::string& body, std::string&) -> uint32 { ++calls; return body == "report" ? 201 : 500; },
		queue.Poster(),
		[&outcomes](const BugReportUploader::Outcome& o) { outcomes.push_back(o); },
		{ 0, 0 });
	uploader.Start();
	CHECK(uploader.Enqueue("report"));

	REQUIRE(WaitFor(queue, [&]() { return !outcomes.empty(); }));
	CHECK(outcomes[0].kind == BugReportUploader::Outcome::Delivered);
	CHECK(outcomes[0].status == 201);
	CHECK(calls == 1);
	uploader.Stop();
}

TEST_CASE("BugReportUploader retries server errors and gives up after the last attempt", "[bug_report]")
{
	PostQueue queue;
	std::vector<BugReportUploader::Outcome> outcomes;
	std::atomic<int> calls{ 0 };

	BugReportUploader uploader(
		[&calls](const std::string&, std::string&) -> uint32 { ++calls; return 503; },
		queue.Poster(),
		[&outcomes](const BugReportUploader::Outcome& o) { outcomes.push_back(o); },
		{ 0, 0 });
	uploader.Start();
	uploader.Enqueue("report");

	REQUIRE(WaitFor(queue, [&]() { return !outcomes.empty() && outcomes.back().kind == BugReportUploader::Outcome::Dropped; }));
	CHECK(calls == 3);
	CHECK(outcomes.size() == 4);
	CHECK(outcomes[0].kind == BugReportUploader::Outcome::AttemptFailed);
	CHECK(outcomes[2].kind == BugReportUploader::Outcome::AttemptFailed);
	uploader.Stop();
}

TEST_CASE("BugReportUploader treats exceptions as network errors and recovers", "[bug_report]")
{
	PostQueue queue;
	std::vector<BugReportUploader::Outcome> outcomes;
	std::atomic<int> calls{ 0 };

	BugReportUploader uploader(
		[&calls](const std::string&, std::string&) -> uint32
		{
			if (++calls == 1)
			{
				throw std::runtime_error("connection refused");
			}
			return 201;
		},
		queue.Poster(),
		[&outcomes](const BugReportUploader::Outcome& o) { outcomes.push_back(o); },
		{ 0 });
	uploader.Start();
	uploader.Enqueue("report");

	REQUIRE(WaitFor(queue, [&]() { return outcomes.size() == 2; }));
	CHECK(outcomes[0].kind == BugReportUploader::Outcome::AttemptFailed);
	CHECK(outcomes[0].detail == "connection refused");
	CHECK(outcomes[1].kind == BugReportUploader::Outcome::Delivered);
	uploader.Stop();
}

TEST_CASE("BugReportUploader drops client errors without retrying", "[bug_report]")
{
	PostQueue queue;
	std::vector<BugReportUploader::Outcome> outcomes;
	std::atomic<int> calls{ 0 };

	BugReportUploader uploader(
		[&calls](const std::string&, std::string& error) -> uint32 { ++calls; error = "bad"; return 400; },
		queue.Poster(),
		[&outcomes](const BugReportUploader::Outcome& o) { outcomes.push_back(o); },
		{ 0, 0, 0 });
	uploader.Start();
	uploader.Enqueue("report");

	REQUIRE(WaitFor(queue, [&]() { return !outcomes.empty(); }));
	CHECK(outcomes[0].kind == BugReportUploader::Outcome::Dropped);
	CHECK(calls == 1);
	uploader.Stop();
}

TEST_CASE("BugReportUploader drops the oldest report when the queue overflows", "[bug_report]")
{
	PostQueue queue;
	std::vector<BugReportUploader::Outcome> outcomes;

	// Not started: nothing is consumed, so the queue fills up.
	BugReportUploader uploader(
		[](const std::string&, std::string&) -> uint32 { return 201; },
		queue.Poster(),
		[&outcomes](const BugReportUploader::Outcome& o) { outcomes.push_back(o); },
		{}, 2);

	CHECK(uploader.Enqueue("a"));
	CHECK(uploader.Enqueue("b"));
	CHECK_FALSE(uploader.Enqueue("c"));
	CHECK(uploader.GetQueueSize() == 2);

	queue.Drain();
	REQUIRE(outcomes.size() == 1);
	CHECK(outcomes[0].kind == BugReportUploader::Outcome::Dropped);
}

TEST_CASE("BugReportUploader stops while waiting for a retry", "[bug_report]")
{
	PostQueue queue;
	std::atomic<int> calls{ 0 };

	BugReportUploader uploader(
		[&calls](const std::string&, std::string&) -> uint32 { ++calls; return 500; },
		queue.Poster(),
		[](const BugReportUploader::Outcome&) {},
		{ 60 * 60 * 1000 });
	uploader.Start();
	uploader.Enqueue("report");

	REQUIRE(WaitFor(queue, [&]() { return calls == 1; }));
	const auto before = std::chrono::steady_clock::now();
	uploader.Stop();
	CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(5));
	CHECK(calls == 1);
}

// ---------------------------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------------------------

TEST_CASE("ParseBugApiUrl understands https, http and file urls", "[bug_report]")
{
	BugApiUrl url;
	REQUIRE(ParseBugApiUrl("https://error.mmo-dev.net", url));
	CHECK(url.scheme == "https");
	CHECK(url.host == "error.mmo-dev.net");
	CHECK(url.port == 443);
	CHECK(url.path.empty());

	REQUIRE(ParseBugApiUrl("http://localhost:3000/prefix/", url));
	CHECK(url.scheme == "http");
	CHECK(url.host == "localhost");
	CHECK(url.port == 3000);
	CHECK(url.path == "/prefix");

	REQUIRE(ParseBugApiUrl("file://C:/temp/bugs", url));
	CHECK(url.scheme == "file");
	CHECK(url.path == "C:/temp/bugs");

	CHECK_FALSE(ParseBugApiUrl("ftp://host", url));
	CHECK_FALSE(ParseBugApiUrl("error.mmo-dev.net", url));
	CHECK_FALSE(ParseBugApiUrl("http://host:99999", url));
	CHECK_FALSE(ParseBugApiUrl("http://host:abc", url));
	CHECK_FALSE(ParseBugApiUrl("http://", url));
	CHECK_FALSE(MakeBugReportTransport("nope", "key"));
}

TEST_CASE("The file transport writes each report as a JSON file", "[bug_report]")
{
	const auto directory = std::filesystem::temp_directory_path() / "mmo_bug_transport_test";
	std::filesystem::remove_all(directory);

	auto transport = MakeBugReportTransport("file://" + directory.generic_string(), "");
	REQUIRE(transport);

	std::string error;
	CHECK(transport("{\"a\":1}", error) == 201);
	CHECK(transport("{\"a\":2}", error) == 201);

	std::vector<std::string> contents;
	for (const auto& entry : std::filesystem::directory_iterator(directory))
	{
		std::ifstream in(entry.path());
		contents.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}
	REQUIRE(contents.size() == 2);
	CHECK(std::find(contents.begin(), contents.end(), "{\"a\":1}") != contents.end());

	std::filesystem::remove_all(directory);
}

// ---------------------------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------------------------

TEST_CASE("SanitizeBugReportClientJson keeps known fields only", "[bug_report]")
{
	const std::string text = R"({
		"version": "0.1.0.42",
		"os": "Windows 11",
		"fps": 144.5,
		"evil": {"nested": true},
		"settings": {"gxResolution": "1920x1080", "number": 3},
		"logTail": "line 1\nline 2"
	})";

	json client;
	std::string logTail;
	REQUIRE(SanitizeBugReportClientJson(text, client, logTail));
	CHECK(client["version"] == "0.1.0.42");
	CHECK(client["os"] == "Windows 11");
	CHECK(client["fps"] == 144.5);
	CHECK_FALSE(client.contains("evil"));
	CHECK_FALSE(client.contains("logTail"));
	CHECK(client["settings"]["gxResolution"] == "1920x1080");
	CHECK_FALSE(client["settings"].contains("number"));
	CHECK(logTail == "line 1\nline 2");
}

TEST_CASE("SanitizeBugReportClientJson rejects non-objects and truncates long values", "[bug_report]")
{
	json client;
	std::string logTail;
	CHECK_FALSE(SanitizeBugReportClientJson("not json", client, logTail));
	CHECK_FALSE(SanitizeBugReportClientJson("[1,2]", client, logTail));

	const json input = { { "gpu", std::string(1000, 'g') }, { "logTail", std::string(100 * 1024, 'l') + "END" } };
	REQUIRE(SanitizeBugReportClientJson(input.dump(), client, logTail));
	CHECK(client["gpu"].get<std::string>().size() == 256);
	CHECK(logTail.size() == 64 * 1024);
	CHECK(logTail.substr(logTail.size() - 3) == "END");
}

TEST_CASE("CountUtf8Characters counts characters, not bytes", "[bug_report]")
{
	CHECK(CountUtf8Characters("abc") == 3);
	CHECK(CountUtf8Characters("\xC3\xA4\xC3\xB6\xC3\xBC") == 3);
	CHECK(CountUtf8Characters("") == 0);
}

TEST_CASE("BuildBugReportDocument produces the API contract", "[bug_report]")
{
	BugReporterIdentity reporter;
	reporter.accountId = 17;
	reporter.characterGuid = 0xFFFFFFFFFFFFFFF0ull;
	reporter.characterName = "Narith";
	reporter.realmName = "Dev Realm";

	game::BugReportPayload payload;
	payload.subjectType = game::bug_report_subject::Item;
	payload.subjectId = 42;
	payload.subjectGuid = 0;
	payload.subjectName = "Kupferrute";
	payload.comment = "Wrong price";

	const json document = BuildBugReportDocument(reporter, payload, json{ { "os", "Win" } }, "log", json{ { "x", 1 } }, "world-01");
	CHECK(document["schemaVersion"] == 1);
	CHECK(document["reporter"]["accountId"] == "17");
	CHECK(document["reporter"]["characterId"] == "18446744073709551600");
	CHECK(document["reporter"]["characterName"] == "Narith");
	CHECK(document["subject"]["type"] == "item");
	CHECK(document["subject"]["id"] == 42);
	CHECK(document["subject"]["guid"] == "0");
	CHECK(document["comment"] == "Wrong price");
	CHECK(document["client"]["os"] == "Win");
	CHECK(document["logTail"] == "log");
	CHECK(document["server"]["x"] == 1);
	CHECK(document["worldNode"] == "world-01");
	CHECK(document["serverBuild"].is_string());
}

TEST_CASE("SerializeBugReportDocument survives invalid UTF-8", "[bug_report]")
{
	const json document = { { "comment", std::string("bad \xFF byte") } };
	const std::string text = SerializeBugReportDocument(document);
	CHECK(text.find("bad") != std::string::npos);
}
