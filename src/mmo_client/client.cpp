// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// This file contains the entry point of the game and takes care of initializing the
// game as well as starting the main loop for the application. This is used on all
// client-supported platforms.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include "discord.h"

#include "base/typedefs.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"
#include "assets/asset_registry.h"

#include "event_loop.h"
#include "console/console.h"
#include "net/login_connector.h"
#include "net/realm_connector.h"
#include "game_states/game_state_mgr.h"
#include "ui/model_renderer.h"

#ifdef _WIN32
#include "fmod_audio/fmod_audio.h"
#else
#include "null_audio/null_audio.h"
#endif

#include <iostream>
#include <fstream>
#include <memory>
#include <set>

#include "game_states/world_state.h"

#include "base/executable_path.h"
#include "client_data/project.h"

#include "systems/inventory_client.h"

#include "ui/minimap.h"

#include "base/create_process.h"
#include "game_client/object_mgr.h"
#include "client_context.h"
#include "client_application.h"

namespace mmo
{
	void DispatchOnGameThread(std::function<void()> &&f)
	{
		GetClientContext().timerService.post(std::move(f));
	}
}

////////////////////////////////////////////////////////////////
// Entry point

namespace mmo
{
	/// Shared entry point of the application on all platforms.
	int32 CommonMain(int argc, char **argv)
	{
		// TODO: Do something with command line arguments

		ClientApplication app;
		if (app.Start())
		{
			EventLoop::Run();
			app.Stop();
		}

		return 0;
	}
}

#ifdef _WIN32
#include "base/win_utility.h"

// Add this helper function before the ExceptionFilterWin32 function
void LogStackTrace(CONTEXT *context, std::ostringstream &output)
{
	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();

	// Initialize symbols
	SymInitialize(process, NULL, TRUE);

	// Set options
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

	// Initialize stack frame
	STACKFRAME64 stack = {};
	stack.AddrPC.Offset = context->Rip;
	stack.AddrPC.Mode = AddrModeFlat;
	stack.AddrFrame.Offset = context->Rbp;
	stack.AddrFrame.Mode = AddrModeFlat;
	stack.AddrStack.Offset = context->Rsp;
	stack.AddrStack.Mode = AddrModeFlat;

	// Reserve space for symbol info
	constexpr int MAX_SYMBOL_NAME = 256;
	constexpr int SIZE_OF_SYMBOL_INFO = sizeof(SYMBOL_INFO) + MAX_SYMBOL_NAME;
	uint8_t buffer[SIZE_OF_SYMBOL_INFO];
	SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = MAX_SYMBOL_NAME;

	// Line info
	IMAGEHLP_LINE64 line = {};
	line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
	DWORD displacement;

	// Helper that resolves a module's short name from its base address. We prefer the name reported by
	// DbgHelp, falling back to the on-disk image name. This keeps frame lines and the MODULES table consistent.
	auto getModuleName = [process](DWORD64 base, std::string& outName) -> bool
	{
		IMAGEHLP_MODULE64 modInfo = {};
		modInfo.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
		if (base != 0 && SymGetModuleInfo64(process, base, &modInfo))
		{
			outName = modInfo.ModuleName;
			return true;
		}
		return false;
	};

	// Remember every module we touch so we can dump their PDB signatures afterwards. The backend uses
	// (module name + RVA) together with the PDB GUID/age to resolve symbols from its private symbol store,
	// so the client never has to ship a PDB.
	std::set<DWORD64> moduleBases;

	// Walk the stack
	for (int frameNum = 0; frameNum < 30; frameNum++)
	{
		BOOL result = StackWalk64(
			IMAGE_FILE_MACHINE_AMD64,
			process,
			thread,
			&stack,
			context,
			NULL,
			SymFunctionTableAccess64,
			SymGetModuleBase64,
			NULL);

		if (!result || stack.AddrPC.Offset == 0)
			break;

		const DWORD64 address = stack.AddrPC.Offset;
		const DWORD64 moduleBase = SymGetModuleBase64(process, address);

		output << "Frame " << frameNum << ": ";

		// Emit a module-relative address (RVA). This is the stable identifier that survives ASLR and lets the
		// backend map the frame back to a function without symbols being present on the crashing machine.
		std::string moduleName;
		if (moduleBase != 0 && getModuleName(moduleBase, moduleName))
		{
			moduleBases.insert(moduleBase);
			const DWORD64 rva = address - moduleBase;
			output << moduleName << "+0x" << std::hex << rva << std::dec;
		}
		else
		{
			output << "<unknown module>";
		}

		// Absolute address kept for reference / cross-checking.
		output << " (0x" << std::hex << address << std::dec << ")";

		// If the crashing machine happens to have matching symbols (e.g. a developer build), include the
		// resolved name and source location inline. On customer machines these simply won't resolve.
		if (SymFromAddr(process, address, NULL, symbol))
		{
			output << " " << symbol->Name;
		}
		if (SymGetLineFromAddr64(process, address, &displacement, &line))
		{
			output << " at " << line.FileName << ":" << line.LineNumber;
		}

		output << "\r\n";
	}

	// Dump the module table with PDB signatures. Each entry gives the backend everything it needs to pick the
	// exact matching PDB from its symbol store: the PDB id (GUID + age, formatted as the canonical symsrv
	// directory name) plus the image base/size. The PDB signature is read from the loaded image's debug
	// directory, so it is available even when the PDB itself is not present on the machine.
	output << "\r\n";
	output << "-----------------------------------------------\r\n";
	output << "MODULES\r\n";
	output << "-----------------------------------------------\r\n";

	for (const DWORD64 base : moduleBases)
	{
		IMAGEHLP_MODULE64 modInfo = {};
		modInfo.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
		if (!SymGetModuleInfo64(process, base, &modInfo))
		{
			continue;
		}

		// Format the PDB GUID as 32 upper-case hex digits (no braces), then append the age as upper-case hex
		// with no leading zeros. Concatenated, this is exactly the <GUID><AGE> folder name used by symsrv:
		//   symbols\mmo_client.pdb\<pdbid>\mmo_client.pdb
		const GUID& g = modInfo.PdbSig70;
		char pdbId[48] = { 0 };
		sprintf_s(pdbId, sizeof(pdbId),
			"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X",
			g.Data1, g.Data2, g.Data3,
			g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
			g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7],
			modInfo.PdbAge);

		// The PDB file name as recorded in the image (CVData holds the original link-time path for RSDS records).
		std::string pdbName = "unknown.pdb";
		if (modInfo.CVData[0] != 0)
		{
			const char* path = modInfo.CVData;
			const char* slash = strrchr(path, '\\');
			const char* fwd = strrchr(path, '/');
			if (fwd > slash)
			{
				slash = fwd;
			}
			pdbName = slash ? slash + 1 : path;
		}

		output << modInfo.ModuleName
			<< "  base=0x" << std::hex << base << std::dec
			<< "  size=0x" << std::hex << modInfo.ImageSize << std::dec
			<< "  pdb=" << pdbName
			<< "  pdbid=" << pdbId
			<< "\r\n";
	}

	SymCleanup(process);
}

LONG WINAPI ExceptionFilterWin32(_In_ struct _EXCEPTION_POINTERS *ExceptionInfo)
{
	// Implement an exception handler which formulates a readable error message, include a stack trace and some more data about the crash and
	// then calls an executable to send the error message to some server of us.

	std::ostringstream exceptionMessageBuffer;
	exceptionMessageBuffer << "Unhandled exception: 0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionCode;

	// Get exception name from exception code
	switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		exceptionMessageBuffer << " EXCEPTION_ACCESS_VIOLATION";
		break;
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		exceptionMessageBuffer << " EXCEPTION_DATATYPE_MISALIGNMENT";
		break;
	case EXCEPTION_BREAKPOINT:
		exceptionMessageBuffer << " EXCEPTION_BREAKPOINT";
		break;
	case EXCEPTION_SINGLE_STEP:
		exceptionMessageBuffer << " EXCEPTION_SINGLE_STEP";
		break;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		exceptionMessageBuffer << " EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
		break;
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		exceptionMessageBuffer << " EXCEPTION_FLT_DENORMAL_OPERAND";
		break;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		exceptionMessageBuffer << " EXCEPTION_FLT_DIVIDE_BY_ZERO";
		break;
	case EXCEPTION_FLT_INEXACT_RESULT:
		exceptionMessageBuffer << " EXCEPTION_FLT_INEXACT_RESULT";
		break;
	case EXCEPTION_FLT_INVALID_OPERATION:
		exceptionMessageBuffer << " EXCEPTION_FLT_INVALID_OPERATION";
		break;
	case EXCEPTION_FLT_OVERFLOW:
		exceptionMessageBuffer << " EXCEPTION_FLT_OVERFLOW";
		break;
	case EXCEPTION_FLT_STACK_CHECK:
		exceptionMessageBuffer << " EXCEPTION_FLT_STACK_CHECK";
		break;
	case EXCEPTION_FLT_UNDERFLOW:
		exceptionMessageBuffer << " EXCEPTION_FLT_UNDERFLOW";
		break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		exceptionMessageBuffer << " EXCEPTION_INT_DIVIDE_BY_ZERO";
		break;
	case EXCEPTION_INT_OVERFLOW:
		exceptionMessageBuffer << " EXCEPTION_INT_OVERFLOW";
		break;
	case EXCEPTION_PRIV_INSTRUCTION:
		exceptionMessageBuffer << " EXCEPTION_PRIV_INSTRUCTION";
		break;
	case EXCEPTION_IN_PAGE_ERROR:
		exceptionMessageBuffer << " EXCEPTION_IN_PAGE_ERROR";
		break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		exceptionMessageBuffer << " EXCEPTION_ILLEGAL_INSTRUCTION";
		break;
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		exceptionMessageBuffer << " EXCEPTION_NONCONTINUABLE_EXCEPTION";
		break;
	case EXCEPTION_STACK_OVERFLOW:
		exceptionMessageBuffer << " EXCEPTION_STACK_OVERFLOW";
		break;
	case EXCEPTION_INVALID_DISPOSITION:
		exceptionMessageBuffer << " EXCEPTION_INVALID_DISPOSITION";
		break;
	case EXCEPTION_GUARD_PAGE:
		exceptionMessageBuffer << " EXCEPTION_GUARD_PAGE";
		break;
	case EXCEPTION_INVALID_HANDLE:
		exceptionMessageBuffer << " EXCEPTION_INVALID_HANDLE";
		break;
	}
	exceptionMessageBuffer << std::endl
						   << std::endl;

	exceptionMessageBuffer << "Exception address: 0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionAddress << std::endl;
	exceptionMessageBuffer << "Exception flags: 0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionFlags << std::endl;

	exceptionMessageBuffer << "Exception parameters: ";
	for (size_t i = 0; i < ExceptionInfo->ExceptionRecord->NumberParameters; ++i)
	{
		exceptionMessageBuffer << "0x" << std::hex << ExceptionInfo->ExceptionRecord->ExceptionInformation[i] << " ";
	}
	exceptionMessageBuffer << std::endl;

	exceptionMessageBuffer << "Context flags: 0x" << std::hex << ExceptionInfo->ContextRecord->ContextFlags << std::endl;
	exceptionMessageBuffer << "Context address: 0x" << std::hex << ExceptionInfo->ContextRecord->Rip << std::endl;
	exceptionMessageBuffer << "Context stack pointer: 0x" << std::hex << ExceptionInfo->ContextRecord->Rsp << std::endl;
	exceptionMessageBuffer << "Context base pointer: 0x" << std::hex << ExceptionInfo->ContextRecord->Rbp << std::endl;
	exceptionMessageBuffer << "Context instruction pointer: 0x" << std::hex << ExceptionInfo->ContextRecord->Rip << std::endl;
	exceptionMessageBuffer << std::endl;

	exceptionMessageBuffer << "-----------------------------------------------\r\n";
	exceptionMessageBuffer << "STACK TRACE\r\n";
	exceptionMessageBuffer << "-----------------------------------------------\r\n";

	// Get the stack trace
	LogStackTrace(ExceptionInfo->ContextRecord, exceptionMessageBuffer);

	// Log some player data if available
	if (mmo::ObjectMgr::GetActivePlayerGuid())
	{
		if (const auto player = mmo::ObjectMgr::GetActivePlayer())
		{
			exceptionMessageBuffer << "-----------------------------------------------\r\n";
			exceptionMessageBuffer << "PLAYER DATA\r\n";
			exceptionMessageBuffer << "-----------------------------------------------\r\n";
			exceptionMessageBuffer << "Active player GUID: " << mmo::log_hex_digit(player->GetGuid()) << "\r\n";
			exceptionMessageBuffer << "Active player name: " << player->GetName() << "\r\n";
			exceptionMessageBuffer << "Active player level: " << player->GetLevel() << "\r\n";
			exceptionMessageBuffer << "Active player map: " << player->GetMapId() << "\r\n";
			exceptionMessageBuffer << "Active player location: " << player->GetPosition() << "\r\n";
			exceptionMessageBuffer << "Active player facing: " << player->GetFacing().GetValueRadians() << "\r\n";
		}
	}

	// Write to a temporary file
	std::filesystem::path tempPath = std::filesystem::temp_directory_path();

	// Ensure filename contains current timestamp to make it more unique
	const auto now = std::chrono::system_clock::now();
	const auto nowTime = std::chrono::system_clock::to_time_t(now);

	std::tm nowTm;
	localtime_s(&nowTm, &nowTime);
	std::ostringstream filename;
	filename << "mmo_error_" << std::put_time(&nowTm, "%Y%m%d_%H%M%S") << ".txt";

	const std::filesystem::path tempFile = tempPath / filename.str();
	std::ofstream errorFile(tempFile.string());
	if (errorFile.is_open())
	{
		errorFile << exceptionMessageBuffer.str();
		errorFile.close();
	}
	else
	{
		std::cerr << "Could not open error file for writing: " << tempFile.string() << std::endl;
	}

	// Flush log file first
	if (mmo::GetClientContext().logFile.is_open())
	{
		mmo::GetClientContext().logFile.flush();
	}

	// Call error sender executable
	mmo::createProcess("./mmo_error.exe", {tempFile.string(), "./Logs/Client.log"});
	return 0;
}

/// Procedural entry point on windows platforms.
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	// In debug builds, we only want to add an exception handler when no debugger is attached. In release builds, we always set this handler
#ifdef _DEBUG
	if (!IsDebuggerPresent())
	{
#endif
		SetUnhandledExceptionFilter(ExceptionFilterWin32);
#ifdef _DEBUG
	}
#endif

	// Setup log to print each log entry to the debug output on windows
#ifdef _DEBUG
	if (IsDebuggerPresent())
	{
		std::mutex logMutex;
		mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry &entry)
										   {
			std::scoped_lock lock{ logMutex };
			OutputDebugStringA((entry.message + "\n").c_str()); });
	}
#endif

	// Split command line arguments
	auto argc = 0;
	auto *const argv = CommandLineToArgvA(GetCommandLine(), &argc);

	// If no debugger is attached, encapsulate the common main function in a try/catch
	// block to catch exceptions and display an error message to the user.
	if (!::IsDebuggerPresent())
	{
		try
		{
			// Run the common main function
			return mmo::CommonMain(argc, argv);
		}
		catch (const std::exception &e)
		{
			// Display the error message
			MessageBoxA(nullptr, e.what(), "Error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
			return 1;
		}
	}

	// Debugger is present, so just run common main function uncatched, as the debugger
	// should handle uncaught exceptions
	return mmo::CommonMain(argc, argv);
}

#else

#ifdef __APPLE__
int main_osx(int argc, char *argv[]);
#endif

/// Procedural entry point on non-windows platforms.
int main(int argc, char **argv)
{
	// Set working directory
	std::filesystem::current_path(mmo::GetExecutablePath());

	// Write everything log entry to cout on non-windows platforms by default
	std::mutex logMutex;
	mmo::g_DefaultLog.signal().connect([&logMutex](const mmo::LogEntry &entry)
									   {
		std::scoped_lock lock{ logMutex };
		mmo::printLogEntry(std::cout, entry, mmo::g_DefaultConsoleLogOptions); });

#ifdef __APPLE__
	return main_osx(argc, argv);
#else
	// Finally, run the common main function on all platforms
	return mmo::CommonMain(argc, argv);
#endif
}

#endif
