// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "crash_report.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace mmo
{
	std::string FormatCrashReport(const CrashReport& report)
	{
		std::ostringstream out;
		out << report.applicationName << "\r\n";
		out << report.reason << "\r\n";

		if (!report.details.empty())
		{
			out << "\r\n";
			for (const auto& detail : report.details)
			{
				out << detail << "\r\n";
			}
		}

		if (!report.frames.empty())
		{
			out << "\r\n";
			out << "-----------------------------------------------\r\n";
			out << "STACK TRACE\r\n";
			out << "-----------------------------------------------\r\n";
		}

		uint32 frameIndex = 0;
		for (const auto& frame : report.frames)
		{
			out << "Frame " << frameIndex << ": ";

			// A module-relative address is the only identifier that survives ASLR, so it is what
			// the offline symbolicator matches against the archived PDB. Frames we cannot attribute
			// to a module still get their absolute address, which is better than dropping them.
			if (!frame.moduleName.empty())
			{
				out << frame.moduleName << "+0x" << std::hex << frame.moduleRva << std::dec;
			}
			else
			{
				out << "<unknown module>";
			}

			out << " (0x" << std::hex << frame.address << std::dec << ")";

			if (!frame.symbolName.empty())
			{
				out << " " << frame.symbolName;
			}

			if (!frame.sourceFile.empty())
			{
				out << " at " << frame.sourceFile << ":" << frame.sourceLine;
			}

			out << "\r\n";
			++frameIndex;
		}

		if (!report.modules.empty())
		{
			out << "\r\n";
			out << "-----------------------------------------------\r\n";
			out << "MODULES\r\n";
			out << "-----------------------------------------------\r\n";

			for (const auto& crashModule : report.modules)
			{
				// The symbolicator anchors this line at its end, so nothing may be appended here.
				out << crashModule.name
					<< "  base=0x" << std::hex << crashModule.base << std::dec
					<< "  size=0x" << std::hex << crashModule.size << std::dec
					<< "  pdb=" << crashModule.pdbName
					<< "  pdbid=" << crashModule.pdbId
					<< "\r\n";
			}
		}

		return out.str();
	}

	std::string BuildCrashReportFileName(const std::string& applicationName, const std::tm& when)
	{
		std::ostringstream out;
		out << applicationName << "_crash_"
			<< std::put_time(&when, "%Y%m%d_%H%M%S")
			<< ".txt";
		return out.str();
	}

	std::filesystem::path WriteCrashReport(const std::filesystem::path& directory, const CrashReport& report, const std::tm& when)
	{
		std::error_code error;
		std::filesystem::create_directories(directory, error);

		const std::filesystem::path target = directory / BuildCrashReportFileName(report.applicationName, when);

		// Binary mode so the \r\n line endings the symbolicator expects survive on every platform.
		std::ofstream file(target.string(), std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return {};
		}

		const std::string text = FormatCrashReport(report);
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		file.close();

		return target;
	}
}
