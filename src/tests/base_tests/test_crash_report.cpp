// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "base/crash_report.h"

#include <filesystem>
#include <fstream>

using namespace mmo;

TEST_CASE("Crash report header names the application and the reason", "[crash_report]")
{
	CrashReport report;
	report.applicationName = "world_server";
	report.reason = "Unhandled exception: 0xc0000005 EXCEPTION_ACCESS_VIOLATION";

	const std::string text = FormatCrashReport(report);

	REQUIRE(text.find("world_server") != std::string::npos);
	REQUIRE(text.find("Unhandled exception: 0xc0000005 EXCEPTION_ACCESS_VIOLATION") != std::string::npos);
}

TEST_CASE("Crash report frame lines match the symbolicator frame format", "[crash_report]")
{
	// tools/symbolicate_crash.ps1 parses frames with the regex
	//   ^Frame\s+(\d+):\s+(\S+)\+0x([0-9a-fA-F]+)\s+\(0x([0-9a-fA-F]+)\)
	// Emitting anything else silently drops the frame from the symbolicated output.
	CrashReport report;
	report.applicationName = "world_server";

	CrashFrame frame;
	frame.moduleName = "world_server";
	frame.moduleRva = 0x1a2b3c;
	frame.address = 0x7ff6001a2b3c;
	report.frames.push_back(frame);

	const std::string text = FormatCrashReport(report);

	REQUIRE(text.find("Frame 0: world_server+0x1a2b3c (0x7ff6001a2b3c)") != std::string::npos);
}

TEST_CASE("A frame with no owning module still reports its absolute address", "[crash_report]")
{
	CrashReport report;

	CrashFrame frame;
	frame.address = 0x7ff600000abc;
	report.frames.push_back(frame);

	const std::string text = FormatCrashReport(report);

	REQUIRE(text.find("Frame 0: <unknown module> (0x7ff600000abc)") != std::string::npos);
}

TEST_CASE("Resolved symbols and source locations are appended to the frame", "[crash_report]")
{
	CrashReport report;

	CrashFrame frame;
	frame.moduleName = "world_server";
	frame.moduleRva = 0x10;
	frame.address = 0x20;
	frame.symbolName = "mmo::WorldInstance::RemoveGameObject";
	frame.sourceFile = "H:\\mmo\\src\\shared\\game_server\\world\\world_instance.cpp";
	frame.sourceLine = 433;
	report.frames.push_back(frame);

	const std::string text = FormatCrashReport(report);

	REQUIRE(text.find("mmo::WorldInstance::RemoveGameObject") != std::string::npos);
	REQUIRE(text.find("world_instance.cpp:433") != std::string::npos);
}

TEST_CASE("Crash report module lines match the symbolicator module format", "[crash_report]")
{
	// tools/symbolicate_crash.ps1 parses modules with the regex
	//   ^(\S+)\s+base=0x[0-9a-fA-F]+\s+size=0x[0-9a-fA-F]+\s+pdb=\S+\s+pdbid=(\S+)\s*$
	// Note the end anchor: nothing may follow the pdbid on the line.
	CrashReport report;

	CrashModule crashModule;
	crashModule.name = "world_server";
	crashModule.base = 0x7ff600000000;
	crashModule.size = 0x1394000;
	crashModule.pdbName = "world_server.pdb";
	crashModule.pdbId = "A1B2C3D4E5F60718293A4B5C6D7E8F901";
	report.modules.push_back(crashModule);

	const std::string text = FormatCrashReport(report);

	REQUIRE(text.find(
		"world_server  base=0x7ff600000000  size=0x1394000  pdb=world_server.pdb  "
		"pdbid=A1B2C3D4E5F60718293A4B5C6D7E8F901\r\n") != std::string::npos);
}

TEST_CASE("Detail lines are rendered verbatim", "[crash_report]")
{
	CrashReport report;
	report.details.push_back("Exception address: 0x7ff6001a2b3c");
	report.details.push_back("World instance: 1 (map 1)");

	const std::string text = FormatCrashReport(report);

	REQUIRE(text.find("Exception address: 0x7ff6001a2b3c\r\n") != std::string::npos);
	REQUIRE(text.find("World instance: 1 (map 1)\r\n") != std::string::npos);
}

TEST_CASE("Crash report file names are sortable and identify the application", "[crash_report]")
{
	std::tm when = {};
	when.tm_year = 2026 - 1900;
	when.tm_mon = 7;	// August
	when.tm_mday = 13;
	when.tm_hour = 13;
	when.tm_min = 0;
	when.tm_sec = 3;

	REQUIRE(BuildCrashReportFileName("world_server", when) == "world_server_crash_20260813_130003.txt");
}

TEST_CASE("Writing a crash report creates the directory and returns the path", "[crash_report]")
{
	const std::filesystem::path directory =
		std::filesystem::temp_directory_path() / "mmo_crash_report_test" / "logs";
	std::filesystem::remove_all(directory.parent_path());

	CrashReport report;
	report.applicationName = "world_server";
	report.reason = "Fatal signal: SIGSEGV";

	std::tm when = {};
	when.tm_year = 2026 - 1900;
	when.tm_mon = 7;
	when.tm_mday = 13;
	when.tm_hour = 13;
	when.tm_min = 0;
	when.tm_sec = 3;

	const std::filesystem::path written = WriteCrashReport(directory, report, when);

	REQUIRE(written == directory / "world_server_crash_20260813_130003.txt");
	REQUIRE(std::filesystem::exists(written));

	std::ifstream file(written.string(), std::ios::binary);
	const std::string contents{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	REQUIRE(contents.find("Fatal signal: SIGSEGV") != std::string::npos);

	file.close();
	std::filesystem::remove_all(directory.parent_path());
}
