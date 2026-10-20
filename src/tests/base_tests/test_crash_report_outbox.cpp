// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/crash_report_outbox.h"

#include <filesystem>
#include <fstream>

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

	const char* const SampleReport =
		"world_server\r\n"
		"Aborted (SIGABRT) - usually a failed assertion\r\n"
		"\r\n"
		"Version: 0.1.0.42 (Commit: abc123)\r\n"
		"Log file: /app/logs/logs_2026-Oct-20.log\r\n"
		"Context 0: client packet 0x0042 from character 0x0000000000000001 'Kyoril'\r\n";
}

TEST_CASE("Unsent crash reports of an application are found oldest first", "[crash_report_outbox]")
{
	const auto directory = MakeEmptyDirectory("mmo_crash_outbox_find");
	WriteFile(directory / "world_server_crash_20261020_120000.txt", "b");
	WriteFile(directory / "world_server_crash_20261019_080000.txt", "a");
	WriteFile(directory / "realm_server_crash_20261019_080000.txt", "other tier");
	WriteFile(directory / "world_server_2026-Oct-20.log", "a log");

	const auto reports = FindUnsentCrashReports(directory, "world_server");

	REQUIRE(reports.size() == 2);
	CHECK(reports[0].filename() == "world_server_crash_20261019_080000.txt");
	CHECK(reports[1].filename() == "world_server_crash_20261020_120000.txt");

	std::filesystem::remove_all(directory);
}

TEST_CASE("A crash report marked sent is not found again", "[crash_report_outbox]")
{
	const auto directory = MakeEmptyDirectory("mmo_crash_outbox_mark");
	const auto report = directory / "login_server_crash_20261020_120000.txt";
	WriteFile(report, "report");

	REQUIRE(MarkCrashReportSent(report, "sent: HTTP 201"));
	CHECK(std::filesystem::exists(GetCrashReportSentMarker(report)));
	CHECK(FindUnsentCrashReports(directory, "login_server").empty());

	// The marker sits next to the report; the report itself stays where the operator was told it is.
	CHECK(std::filesystem::exists(report));

	std::filesystem::remove_all(directory);
}

TEST_CASE("A missing report directory yields no reports", "[crash_report_outbox]")
{
	CHECK(FindUnsentCrashReports(std::filesystem::temp_directory_path() / "mmo_crash_outbox_does_not_exist", "world_server").empty());
}

TEST_CASE("Crash report details and the reason are read back", "[crash_report_outbox]")
{
	CHECK(ReadCrashReportDetail(SampleReport, CrashReportLogFileKey) == "/app/logs/logs_2026-Oct-20.log");
	CHECK(ReadCrashReportDetail(SampleReport, "Version") == "0.1.0.42 (Commit: abc123)");
	CHECK(ReadCrashReportDetail(SampleReport, "Missing").empty());
	CHECK(ReadCrashReportReason(SampleReport) == "Aborted (SIGABRT) - usually a failed assertion");
	CHECK(ReadCrashReportReason("only one line").empty());
}

TEST_CASE("A file tail is cut at a line boundary", "[crash_report_outbox]")
{
	const auto directory = MakeEmptyDirectory("mmo_crash_outbox_tail");
	const auto log = directory / "server.log";
	WriteFile(log, "first line\nsecond line\nthird line\n");

	CHECK(ReadFileTail(log, 1024) == "first line\nsecond line\nthird line\n");

	// 15 bytes start inside "second line", so the fragment is dropped.
	CHECK(ReadFileTail(log, 15) == "third line\n");

	CHECK(ReadFileTail(directory / "missing.log", 1024).empty());

	std::filesystem::remove_all(directory);
}

TEST_CASE("The upload content carries the report and the log under mmo_error headers", "[crash_report_outbox]")
{
	CrashUpload upload;
	upload.reportPath = "logs/world_server_crash_20261020_120000.txt";
	upload.reportText = "Frame 0: world_server+0x1a2b (0x7ff61a2b)\r\n";
	upload.logPath = "logs/world.log";
	upload.logTail = "last log line\n";

	const std::string content = BuildCrashUploadContent(upload);

	const auto reportHeader = content.find("=== Log File: logs/world_server_crash_20261020_120000.txt");
	const auto frame = content.find("Frame 0: world_server+0x1a2b (0x7ff61a2b)");
	const auto logHeader = content.find("=== Log File: logs/world.log");
	const auto logLine = content.find("last log line");

	REQUIRE(reportHeader != std::string::npos);
	REQUIRE(frame != std::string::npos);
	REQUIRE(logHeader != std::string::npos);
	REQUIRE(logLine != std::string::npos);
	CHECK(reportHeader < frame);
	CHECK(frame < logHeader);
	CHECK(logHeader < logLine);
}

TEST_CASE("The upload body is the multipart form the crash API takes", "[crash_report_outbox]")
{
	CrashUpload upload;
	upload.reportPath = "report.txt";
	upload.reportText = "world_server\r\nreason\r\n";
	upload.appVersion = "0.1.0.42";
	upload.osVersion = "Linux";
	upload.deviceModel = "world_server @ node-01";
	upload.errorMessage = "world_server crash: reason";
	upload.userInput = "automatic";

	const std::string body = BuildCrashUploadBody(upload, "BOUNDARY");

	CHECK(body.compare(0, 12, "--BOUNDARY\r\n") == 0);
	CHECK(body.find("name=\"log_content\"; filename=\"crash-report.txt\"") != std::string::npos);
	CHECK(body.find("name=\"appVersion\"\r\n\r\n0.1.0.42\r\n") != std::string::npos);
	CHECK(body.find("name=\"osVersion\"\r\n\r\nLinux\r\n") != std::string::npos);
	CHECK(body.find("name=\"deviceModel\"\r\n\r\nworld_server @ node-01\r\n") != std::string::npos);
	CHECK(body.find("name=\"errorMessage\"\r\n\r\nworld_server crash: reason\r\n") != std::string::npos);
	CHECK(body.find("name=\"user_input\"\r\n\r\nautomatic\r\n") != std::string::npos);
	CHECK(body.size() >= 14);
	CHECK(body.compare(body.size() - 14, 14, "--BOUNDARY--\r\n") == 0);
}

TEST_CASE("Only transient upload failures are retried", "[crash_report_outbox]")
{
	CHECK(IsRetryableCrashUploadStatus(0));
	CHECK(IsRetryableCrashUploadStatus(408));
	CHECK(IsRetryableCrashUploadStatus(429));
	CHECK(IsRetryableCrashUploadStatus(500));
	CHECK(IsRetryableCrashUploadStatus(503));
	CHECK_FALSE(IsRetryableCrashUploadStatus(400));
	CHECK_FALSE(IsRetryableCrashUploadStatus(413));
}
