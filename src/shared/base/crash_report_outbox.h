// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace mmo
{
	/// Detail key under which a server crash report names the log file that was open when it
	/// crashed. The uploader reads it back to attach the log tail.
	constexpr const char* CrashReportLogFileKey = "Log file";

	/// Everything that goes into one upload to the crash API (POST /api/reports), which takes the
	/// same multipart form the client's mmo_error uploader sends.
	struct CrashUpload
	{
		/// Path of the crash report, used as its section header in the uploaded text.
		std::string reportPath;

		/// Full text of the crash report.
		std::string reportText;

		/// Path of the attached log, or empty when there is none.
		std::string logPath;

		/// Tail of the attached log.
		std::string logTail;

		std::string appVersion;
		std::string osVersion;
		std::string deviceModel;
		std::string errorMessage;
		std::string userInput;
	};

	/// @returns The marker file that flags @p report as handled: "<report>.sent".
	std::filesystem::path GetCrashReportSentMarker(const std::filesystem::path& report);

	/// Lists the crash reports of an application that have not been handed to the crash API yet,
	/// oldest first.
	/// @param directory Directory the crash handler writes reports to.
	/// @param applicationName Application whose reports to list, e.g. "world_server".
	/// @returns Paths of "<applicationName>_crash_*.txt" files without a sent marker.
	std::vector<std::filesystem::path> FindUnsentCrashReports(const std::filesystem::path& directory, const std::string& applicationName);

	/// Flags a report as handled so it is not uploaded again.
	/// @param report The report.
	/// @param note Written into the marker, e.g. the HTTP status the upload got.
	/// @returns False if the marker could not be written.
	bool MarkCrashReportSent(const std::filesystem::path& report, const std::string& note);

	/// Reads the value of a "key: value" detail line of a crash report.
	/// @returns The value without surrounding whitespace, or an empty string if there is no such line.
	std::string ReadCrashReportDetail(const std::string& reportText, const std::string& key);

	/// @returns The second line of a crash report, which holds the crash reason.
	std::string ReadCrashReportReason(const std::string& reportText);

	/// Reads at most the last @p maxBytes of a file, starting at a line boundary when it has to cut.
	/// @returns The tail, or an empty string if the file cannot be read.
	std::string ReadFileTail(const std::filesystem::path& file, std::size_t maxBytes);

	/// Builds the "log_content" text of an upload: the report followed by the log tail, each under
	/// the same "=== Log File:" header mmo_error writes, which tools/symbolicate_crash.ps1 reads.
	std::string BuildCrashUploadContent(const CrashUpload& upload);

	/// Builds the multipart/form-data body of an upload.
	/// @param upload What to upload.
	/// @param boundary Multipart boundary; must not occur in the content.
	std::string BuildCrashUploadBody(const CrashUpload& upload, const std::string& boundary);

	/// @returns True if an upload that got @p status is worth retrying later: no response at all
	///          (0), a timeout, rate limiting or a server error. Any other failure would fail again.
	bool IsRetryableCrashUploadStatus(uint32 status);
}
