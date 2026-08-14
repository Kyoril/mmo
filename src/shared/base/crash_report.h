// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace mmo
{
	/// A single resolved stack frame of a crash report.
	struct CrashFrame
	{
		/// Absolute instruction pointer of the frame.
		uint64 address = 0;

		/// Short name of the module the frame belongs to, without extension. Empty if the
		/// frame could not be attributed to a loaded module.
		std::string moduleName;

		/// Offset of @ref address into the owning module. This is what survives ASLR and what
		/// the symbolicator resolves against the archived PDB.
		uint64 moduleRva = 0;

		/// Demangled function name, if symbols happened to be available on the crashing
		/// machine. Empty otherwise — the symbolicator resolves those frames offline.
		std::string symbolName;

		/// Source file of the frame, if line information was available. Empty otherwise.
		std::string sourceFile;

		/// Source line of the frame. Only meaningful when @ref sourceFile is set.
		uint32 sourceLine = 0;
	};

	/// A loaded module referenced by at least one frame of a crash report.
	struct CrashModule
	{
		/// Short module name without extension, matching @ref CrashFrame::moduleName.
		std::string name;

		/// Load address of the module.
		uint64 base = 0;

		/// Size of the loaded image in bytes.
		uint64 size = 0;

		/// File name of the module's debug database, e.g. "world_server.pdb".
		std::string pdbName;

		/// GUID + age of the debug database, formatted as the canonical symbol-store directory
		/// name. This is what lets the symbolicator pick the exact matching PDB offline.
		std::string pdbId;
	};

	/// All data gathered about a crash, in a platform independent form.
	struct CrashReport
	{
		/// Name of the crashing application, e.g. "world_server".
		std::string applicationName;

		/// Human readable description of what went wrong, e.g.
		/// "Unhandled exception: 0xc0000005 EXCEPTION_ACCESS_VIOLATION".
		std::string reason;

		/// Free-form "key: value" lines describing the crash context — exception address and
		/// flags, register values, and whatever the crashing application chooses to add.
		std::vector<std::string> details;

		/// The captured call stack, innermost frame first.
		std::vector<CrashFrame> frames;

		/// Every module referenced by at least one frame, with the debug-database identity the
		/// offline symbolicator needs.
		std::vector<CrashModule> modules;
	};

	/// Renders a crash report as text, in the format tools/symbolicate_crash.ps1 parses.
	/// @param report The report to render.
	/// @returns The rendered report.
	std::string FormatCrashReport(const CrashReport& report);

	/// Builds the file name a crash report should be written to.
	/// @param applicationName Name of the crashing application, e.g. "world_server".
	/// @param when Local time of the crash.
	/// @returns A file name of the form "<application>_crash_<YYYYMMDD>_<HHMMSS>.txt".
	std::string BuildCrashReportFileName(const std::string& applicationName, const std::tm& when);

	/// Writes a crash report to disk, creating the target directory if needed.
	/// @param directory Directory the report is written to.
	/// @param report The report to write.
	/// @param when Local time of the crash, used for the file name.
	/// @returns The path written, or an empty path if the report could not be written.
	std::filesystem::path WriteCrashReport(const std::filesystem::path& directory, const CrashReport& report, const std::tm& when);
}
