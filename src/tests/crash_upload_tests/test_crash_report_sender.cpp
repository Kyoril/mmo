// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/crash_report_outbox.h"
#include "crash_upload/crash_report_sender.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

using namespace mmo;

namespace
{
	std::filesystem::path MakeEmptyDirectory(const char* name)
	{
		const std::filesystem::path directory = std::filesystem::temp_directory_path() / name;
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
		return directory;
	}

	void WriteFile(const std::filesystem::path& path, const std::string& content)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << content;
	}

	std::string ReadFile(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	struct Recorder
	{
		std::vector<std::string> bodies;
		std::vector<std::string> contentTypes;
		std::vector<CrashReportSender::Outcome> outcomes;
		uint32 status = 201;

		CrashReportSender::Transport Transport()
		{
			return [this](const std::string& body, const std::string& contentType, std::string&) -> uint32
			{
				bodies.push_back(body);
				contentTypes.push_back(contentType);
				return status;
			};
		}

		CrashReportSender::OutcomeHandler Handler()
		{
			return [this](const CrashReportSender::Outcome& outcome)
			{
				outcomes.push_back(outcome);
			};
		}
	};

	CrashReportSender::Config MakeConfig(const std::filesystem::path& directory)
	{
		CrashReportSender::Config config;
		config.apiUrl = "https://error.mmo-dev.net";
		config.applicationName = "world_server";
		config.reportDirectory = directory;
		config.appVersion = "0.1.0.99 (Commit: newer)";
		config.nodeName = "node-01";
		return config;
	}
}

TEST_CASE("A pending crash report is uploaded with its log tail and marked sent", "[crash_report_sender]")
{
	const auto directory = MakeEmptyDirectory("mmo_crash_sender_upload");
	const auto log = directory / "world.log";
	WriteFile(log, "INFO  Player Kyoril requested revive\n");

	const auto report = directory / "world_server_crash_20261020_120000.txt";
	WriteFile(report,
		"world_server\r\n"
		"Aborted (SIGABRT) - usually a failed assertion\r\n"
		"\r\n"
		"Version: 0.1.0.42 (Commit: abc123)\r\n"
		"Log file: " + log.string() + "\r\n"
		"Context 0: client packet 0x0042 from character 0x0000000000000001 'Kyoril'\r\n");

	Recorder recorder;
	CrashReportSender sender(MakeConfig(directory), {}, recorder.Handler(), recorder.Transport());

	CHECK(sender.SendPending() == 1);

	REQUIRE(recorder.bodies.size() == 1);
	const std::string& body = recorder.bodies[0];
	CHECK(body.find("Context 0: client packet 0x0042") != std::string::npos);
	CHECK(body.find("Player Kyoril requested revive") != std::string::npos);
	// The report's own build wins over the build that happens to send it.
	CHECK(body.find("name=\"appVersion\"\r\n\r\n0.1.0.42 (Commit: abc123)\r\n") != std::string::npos);
	CHECK(body.find("name=\"errorMessage\"\r\n\r\nworld_server crash: Aborted (SIGABRT) - usually a failed assertion\r\n") != std::string::npos);
	CHECK(body.find("name=\"deviceModel\"\r\n\r\nworld_server @ node-01\r\n") != std::string::npos);

	REQUIRE(recorder.contentTypes.size() == 1);
	const std::string boundaryPrefix = "multipart/form-data; boundary=";
	REQUIRE(recorder.contentTypes[0].compare(0, boundaryPrefix.size(), boundaryPrefix) == 0);
	const std::string boundary = recorder.contentTypes[0].substr(boundaryPrefix.size());
	CHECK(body.compare(0, boundary.size() + 2, "--" + boundary) == 0);

	REQUIRE(recorder.outcomes.size() == 1);
	CHECK(recorder.outcomes[0].kind == CrashReportSender::Outcome::Sent);
	CHECK(std::filesystem::exists(GetCrashReportSentMarker(report)));

	// A second start has nothing left to send.
	CHECK(sender.SendPending() == 0);
	CHECK(recorder.bodies.size() == 1);

	std::filesystem::remove_all(directory);
}

TEST_CASE("A crash report the API cannot be reached for stays pending", "[crash_report_sender]")
{
	const auto directory = MakeEmptyDirectory("mmo_crash_sender_deferred");
	WriteFile(directory / "world_server_crash_20261019_080000.txt", "world_server\r\nreason\r\n");
	WriteFile(directory / "world_server_crash_20261020_120000.txt", "world_server\r\nreason\r\n");

	Recorder recorder;
	recorder.status = 503;
	CrashReportSender sender(MakeConfig(directory), {}, recorder.Handler(), recorder.Transport());

	CHECK(sender.SendPending() == 0);

	// The first failure stops the run: the second report would fare no better.
	CHECK(recorder.bodies.size() == 1);
	REQUIRE(recorder.outcomes.size() == 1);
	CHECK(recorder.outcomes[0].kind == CrashReportSender::Outcome::Deferred);
	CHECK(FindUnsentCrashReports(directory, "world_server").size() == 2);

	std::filesystem::remove_all(directory);
}

TEST_CASE("A crash report the API rejects is not sent again", "[crash_report_sender]")
{
	const auto directory = MakeEmptyDirectory("mmo_crash_sender_rejected");
	const auto report = directory / "world_server_crash_20261020_120000.txt";
	WriteFile(report, "world_server\r\nreason\r\n");

	Recorder recorder;
	recorder.status = 400;
	CrashReportSender sender(MakeConfig(directory), {}, recorder.Handler(), recorder.Transport());

	CHECK(sender.SendPending() == 1);
	REQUIRE(recorder.outcomes.size() == 1);
	CHECK(recorder.outcomes[0].kind == CrashReportSender::Outcome::Rejected);
	CHECK(ReadFile(GetCrashReportSentMarker(report)).find("rejected: HTTP 400") != std::string::npos);

	std::filesystem::remove_all(directory);
}

TEST_CASE("At most a bounded number of crash reports is sent per start", "[crash_report_sender]")
{
	const auto directory = MakeEmptyDirectory("mmo_crash_sender_bounded");
	for (size_t i = 0; i < CrashReportSender::MaxReportsPerStart + 3; ++i)
	{
		WriteFile(directory / ("world_server_crash_20261020_1200" + std::to_string(10 + i) + ".txt"), "world_server\r\nreason\r\n");
	}

	Recorder recorder;
	CrashReportSender sender(MakeConfig(directory), {}, recorder.Handler(), recorder.Transport());

	CHECK(sender.SendPending() == CrashReportSender::MaxReportsPerStart);
	CHECK(FindUnsentCrashReports(directory, "world_server").size() == 3);

	std::filesystem::remove_all(directory);
}

TEST_CASE("The file transport writes each upload into its directory", "[crash_report_sender]")
{
	const auto directory = MakeEmptyDirectory("mmo_crash_sender_file");
	const auto target = directory / "uploads";

	const auto transport = CrashReportSender::MakeTransport("file://" + target.generic_string(), nullptr);
	REQUIRE(transport);

	std::string error;
	CHECK(transport("body", "text/plain", error) == 201);

	size_t files = 0;
	for (const auto& entry : std::filesystem::directory_iterator(target))
	{
		CHECK(ReadFile(entry.path()) == "body");
		++files;
	}
	CHECK(files == 1);

	CHECK_FALSE(CrashReportSender::MakeTransport("ftp://host", nullptr));

	std::filesystem::remove_all(directory);
}

TEST_CASE("The background worker delivers its outcomes through the poster", "[crash_report_sender]")
{
	const auto directory = MakeEmptyDirectory("mmo_crash_sender_worker");
	WriteFile(directory / "world_server_crash_20261020_120000.txt", "world_server\r\nreason\r\n");

	Recorder recorder;
	std::vector<std::function<void()>> posted;
	std::mutex postedMutex;

	{
		CrashReportSender sender(MakeConfig(directory),
			[&posted, &postedMutex](std::function<void()> work)
			{
				std::scoped_lock lock{ postedMutex };
				posted.push_back(std::move(work));
			},
			recorder.Handler(), recorder.Transport());
		sender.Start();

		// Stop() abandons reports the worker has not reached yet, so wait for it to get there.
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		for (;;)
		{
			{
				std::scoped_lock lock{ postedMutex };
				if (!posted.empty() || std::chrono::steady_clock::now() > deadline)
				{
					break;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		sender.Stop();
	}

	// Nothing ran on the worker thread itself; the owner runs the handlers.
	CHECK(recorder.outcomes.empty());
	REQUIRE(posted.size() == 1);
	posted[0]();
	REQUIRE(recorder.outcomes.size() == 1);
	CHECK(recorder.outcomes[0].kind == CrashReportSender::Outcome::Sent);

	std::filesystem::remove_all(directory);
}
