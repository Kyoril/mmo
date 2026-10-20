// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "crash_report_outbox.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace mmo
{
	namespace
	{
		std::string Trim(const std::string& text)
		{
			const auto first = text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
			{
				return {};
			}

			const auto last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}

		void AppendSection(std::string& out, const std::string& path, const std::string& content)
		{
			out += "=================================================================\r\n";
			out += "=== Log File: " + path + "\r\n";
			out += "=================================================================\r\n\r\n";
			out += content;
			out += "\r\n\r\n";
		}

		void AppendField(std::string& out, const std::string& boundary, const std::string& name, const std::string& value)
		{
			out += "--" + boundary + "\r\n";
			out += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
			out += value + "\r\n";
		}
	}

	std::filesystem::path GetCrashReportSentMarker(const std::filesystem::path& report)
	{
		std::filesystem::path marker = report;
		marker += ".sent";
		return marker;
	}

	std::vector<std::filesystem::path> FindUnsentCrashReports(const std::filesystem::path& directory, const std::string& applicationName)
	{
		std::vector<std::filesystem::path> result;

		std::error_code error;
		std::filesystem::directory_iterator it(directory, error);
		if (error)
		{
			return result;
		}

		const std::string prefix = applicationName + "_crash_";
		for (const std::filesystem::directory_iterator end; it != end; it.increment(error))
		{
			if (error)
			{
				break;
			}

			if (!it->is_regular_file(error))
			{
				continue;
			}

			const std::string name = it->path().filename().string();
			if (name.size() <= prefix.size() + 4 ||
				name.compare(0, prefix.size(), prefix) != 0 ||
				name.compare(name.size() - 4, 4, ".txt") != 0)
			{
				continue;
			}

			if (std::filesystem::exists(GetCrashReportSentMarker(it->path()), error))
			{
				continue;
			}

			result.push_back(it->path());
		}

		// The file name carries the crash time as YYYYMMDD_HHMMSS, so name order is time order.
		std::sort(result.begin(), result.end(), [](const std::filesystem::path& a, const std::filesystem::path& b)
		{
			return a.filename().string() < b.filename().string();
		});

		return result;
	}

	bool MarkCrashReportSent(const std::filesystem::path& report, const std::string& note)
	{
		std::ofstream marker(GetCrashReportSentMarker(report), std::ios::binary | std::ios::trunc);
		if (!marker)
		{
			return false;
		}

		marker << note << "\n";
		return static_cast<bool>(marker);
	}

	std::string ReadCrashReportDetail(const std::string& reportText, const std::string& key)
	{
		const std::string prefix = key + ":";

		std::istringstream lines(reportText);
		std::string line;
		while (std::getline(lines, line))
		{
			if (line.compare(0, prefix.size(), prefix) == 0)
			{
				return Trim(line.substr(prefix.size()));
			}
		}

		return {};
	}

	std::string ReadCrashReportReason(const std::string& reportText)
	{
		std::istringstream lines(reportText);
		std::string line;
		std::getline(lines, line);
		if (!std::getline(lines, line))
		{
			return {};
		}

		return Trim(line);
	}

	std::string ReadFileTail(const std::filesystem::path& file, const std::size_t maxBytes)
	{
		std::ifstream in(file, std::ios::binary | std::ios::ate);
		if (!in)
		{
			return {};
		}

		const std::streamoff size = in.tellg();
		if (size <= 0)
		{
			return {};
		}

		const std::streamoff start = size > static_cast<std::streamoff>(maxBytes) ? size - static_cast<std::streamoff>(maxBytes) : 0;
		in.seekg(start);

		std::string tail(static_cast<std::size_t>(size - start), '\0');
		in.read(tail.data(), static_cast<std::streamsize>(tail.size()));
		tail.resize(static_cast<std::size_t>(in.gcount()));

		// A cut through the middle of a line leaves a fragment that reads like a corrupted entry.
		if (start > 0)
		{
			const auto newline = tail.find('\n');
			if (newline != std::string::npos)
			{
				tail.erase(0, newline + 1);
			}
		}

		return tail;
	}

	std::string BuildCrashUploadContent(const CrashUpload& upload)
	{
		std::string out;
		AppendSection(out, upload.reportPath, upload.reportText);

		if (!upload.logPath.empty())
		{
			AppendSection(out, upload.logPath, upload.logTail.empty() ? std::string("(log file missing or empty)") : upload.logTail);
		}

		return out;
	}

	std::string BuildCrashUploadBody(const CrashUpload& upload, const std::string& boundary)
	{
		std::string out;
		out += "--" + boundary + "\r\n";
		out += "Content-Disposition: form-data; name=\"log_content\"; filename=\"crash-report.txt\"\r\n";
		out += "Content-Type: text/plain\r\n\r\n";
		out += BuildCrashUploadContent(upload) + "\r\n";

		AppendField(out, boundary, "user_input", upload.userInput);
		AppendField(out, boundary, "appVersion", upload.appVersion);
		AppendField(out, boundary, "osVersion", upload.osVersion);
		AppendField(out, boundary, "deviceModel", upload.deviceModel);
		AppendField(out, boundary, "errorMessage", upload.errorMessage);

		out += "--" + boundary + "--\r\n";
		return out;
	}

	bool IsRetryableCrashUploadStatus(const uint32 status)
	{
		return status == 0 || status == 408 || status == 429 || status >= 500;
	}
}
