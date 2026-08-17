// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "crash_handler.h"

#include "crash_report.h"

#include "log/default_log_levels.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#	include <Windows.h>
#	include <DbgHelp.h>
#	pragma comment(lib, "dbghelp.lib")
#else
#	include <csignal>
#	include <dlfcn.h>
#	include <execinfo.h>
#	include <unistd.h>
#endif

namespace mmo
{
	namespace
	{
		/// The active configuration, deliberately leaked. CrashHandlerConfig owns a string, a path
		/// and a std::function, so a plain global would be destroyed during static destruction —
		/// and a fault in a later static destructor would then read freed storage from inside the
		/// handler. Never destroying it is the point.
		CrashHandlerConfig& Config()
		{
			static CrashHandlerConfig* config = new CrashHandlerConfig();
			return *config;
		}

		/// Guards against a crash inside the crash handler turning into an infinite loop. Atomic
		/// rather than volatile because the login server runs two io threads and both could fault
		/// at once, and DbgHelp is not thread safe.
		std::atomic<bool> g_handling { false };

		/// @returns True if this thread won the race to handle the crash.
		bool ClaimCrashHandling()
		{
			return !g_handling.exchange(true);
		}

		std::tm LocalTimeNow()
		{
			const std::time_t now = std::time(nullptr);
			std::tm result = {};
#ifdef _WIN32
			localtime_s(&result, &now);
#else
			localtime_r(&now, &result);
#endif
			return result;
		}

		/// Writes the report and tells the operator where it went. The message goes to stderr
		/// because the log file is already being torn down at this point.
		void FinishReport(CrashReport& report)
		{
			if (Config().onCrash)
			{
				Config().onCrash(report.details);
			}

			const std::filesystem::path written =
				WriteCrashReport(Config().outputDirectory, report, LocalTimeNow());

			if (written.empty())
			{
				std::cerr << "\n=== " << report.applicationName << " CRASHED ===\n"
					<< report.reason << "\n"
					<< "Could not write a crash report to " << Config().outputDirectory.string() << "\n"
					<< FormatCrashReport(report) << std::endl;
				return;
			}

			std::cerr << "\n=== " << report.applicationName << " CRASHED ===\n"
				<< report.reason << "\n"
				<< "Crash report written to " << written.string() << "\n"
				<< "Symbolicate it with: powershell -File tools/symbolicate_crash.ps1 -CrashFile \""
				<< written.string() << "\"" << std::endl;

			if (Config().onReportWritten)
			{
				Config().onReportWritten(written);
			}
		}

#ifdef _WIN32
		const char* DescribeExceptionCode(const DWORD code)
		{
			switch (code)
			{
			case EXCEPTION_ACCESS_VIOLATION:		return "EXCEPTION_ACCESS_VIOLATION";
			case EXCEPTION_DATATYPE_MISALIGNMENT:	return "EXCEPTION_DATATYPE_MISALIGNMENT";
			case EXCEPTION_BREAKPOINT:				return "EXCEPTION_BREAKPOINT";
			case EXCEPTION_SINGLE_STEP:				return "EXCEPTION_SINGLE_STEP";
			case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:	return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
			case EXCEPTION_FLT_DENORMAL_OPERAND:	return "EXCEPTION_FLT_DENORMAL_OPERAND";
			case EXCEPTION_FLT_DIVIDE_BY_ZERO:		return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
			case EXCEPTION_FLT_INEXACT_RESULT:		return "EXCEPTION_FLT_INEXACT_RESULT";
			case EXCEPTION_FLT_INVALID_OPERATION:	return "EXCEPTION_FLT_INVALID_OPERATION";
			case EXCEPTION_FLT_OVERFLOW:			return "EXCEPTION_FLT_OVERFLOW";
			case EXCEPTION_FLT_STACK_CHECK:			return "EXCEPTION_FLT_STACK_CHECK";
			case EXCEPTION_FLT_UNDERFLOW:			return "EXCEPTION_FLT_UNDERFLOW";
			case EXCEPTION_INT_DIVIDE_BY_ZERO:		return "EXCEPTION_INT_DIVIDE_BY_ZERO";
			case EXCEPTION_INT_OVERFLOW:			return "EXCEPTION_INT_OVERFLOW";
			case EXCEPTION_PRIV_INSTRUCTION:		return "EXCEPTION_PRIV_INSTRUCTION";
			case EXCEPTION_IN_PAGE_ERROR:			return "EXCEPTION_IN_PAGE_ERROR";
			case EXCEPTION_ILLEGAL_INSTRUCTION:		return "EXCEPTION_ILLEGAL_INSTRUCTION";
			case EXCEPTION_NONCONTINUABLE_EXCEPTION:return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
			case EXCEPTION_STACK_OVERFLOW:			return "EXCEPTION_STACK_OVERFLOW";
			case EXCEPTION_INVALID_DISPOSITION:		return "EXCEPTION_INVALID_DISPOSITION";
			case EXCEPTION_GUARD_PAGE:				return "EXCEPTION_GUARD_PAGE";
			case EXCEPTION_INVALID_HANDLE:			return "EXCEPTION_INVALID_HANDLE";
			default:								return "UNKNOWN";
			}
		}

		/// Formats a PDB GUID + age as the canonical symbol-store directory name, which is what
		/// symsrv and tools/symbolicate_crash.ps1 use to locate the matching PDB.
		std::string FormatPdbId(const GUID& guid, const DWORD age)
		{
			char buffer[48] = {};
			sprintf_s(buffer, sizeof(buffer),
				"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X",
				guid.Data1, guid.Data2, guid.Data3,
				guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
				guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7],
				age);
			return buffer;
		}

		/// Extracts the bare PDB file name from the link-time path recorded in the image.
		std::string ExtractPdbName(const char* cvData)
		{
			if (cvData == nullptr || cvData[0] == 0)
			{
				return "unknown.pdb";
			}

			const char* backslash = strrchr(cvData, '\\');
			const char* forward = strrchr(cvData, '/');
			const char* separator = (forward > backslash) ? forward : backslash;
			return separator ? separator + 1 : cvData;
		}

		void CaptureStack(CONTEXT* context, CrashReport& report)
		{
			const HANDLE process = GetCurrentProcess();
			const HANDLE thread = GetCurrentThread();

			// Options first: SymInitialize with fInvadeProcess enumerates modules immediately, and
			// line information is only loaded for them if SYMOPT_LOAD_LINES is already set.
			SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
			SymInitialize(process, nullptr, TRUE);

			STACKFRAME64 stack = {};
			stack.AddrPC.Offset = context->Rip;
			stack.AddrPC.Mode = AddrModeFlat;
			stack.AddrFrame.Offset = context->Rbp;
			stack.AddrFrame.Mode = AddrModeFlat;
			stack.AddrStack.Offset = context->Rsp;
			stack.AddrStack.Mode = AddrModeFlat;

			constexpr int MaxSymbolName = 256;
			// SYMBOL_INFO contains ULONG64 members, so the backing storage has to be aligned for it.
			alignas(SYMBOL_INFO) uint8_t symbolBuffer[sizeof(SYMBOL_INFO) + MaxSymbolName] = {};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = MaxSymbolName;

			IMAGEHLP_LINE64 line = {};
			line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

			std::vector<DWORD64> moduleBases;

			constexpr int MaxFrames = 64;
			for (int frameNum = 0; frameNum < MaxFrames; ++frameNum)
			{
				const BOOL walked = StackWalk64(
					IMAGE_FILE_MACHINE_AMD64,
					process,
					thread,
					&stack,
					context,
					nullptr,
					SymFunctionTableAccess64,
					SymGetModuleBase64,
					nullptr);

				if (!walked || stack.AddrPC.Offset == 0)
				{
					break;
				}

				const DWORD64 address = stack.AddrPC.Offset;
				const DWORD64 moduleBase = SymGetModuleBase64(process, address);

				CrashFrame frame;
				frame.address = address;

				IMAGEHLP_MODULE64 moduleInfo = {};
				moduleInfo.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
				if (moduleBase != 0 && SymGetModuleInfo64(process, moduleBase, &moduleInfo))
				{
					frame.moduleName = moduleInfo.ModuleName;
					frame.moduleRva = address - moduleBase;

					if (std::find(moduleBases.begin(), moduleBases.end(), moduleBase) == moduleBases.end())
					{
						moduleBases.push_back(moduleBase);
					}
				}

				// Only resolves on a machine that happens to have matching symbols; on every other
				// machine the module+RVA above is what the offline symbolicator uses.
				if (SymFromAddr(process, address, nullptr, symbol))
				{
					frame.symbolName = symbol->Name;
				}

				DWORD displacement = 0;
				if (SymGetLineFromAddr64(process, address, &displacement, &line))
				{
					frame.sourceFile = line.FileName;
					frame.sourceLine = line.LineNumber;
				}

				report.frames.push_back(std::move(frame));
			}

			for (const DWORD64 base : moduleBases)
			{
				IMAGEHLP_MODULE64 moduleInfo = {};
				moduleInfo.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
				if (!SymGetModuleInfo64(process, base, &moduleInfo))
				{
					continue;
				}

				CrashModule crashModule;
				crashModule.name = moduleInfo.ModuleName;
				crashModule.base = base;
				crashModule.size = moduleInfo.ImageSize;
				crashModule.pdbName = ExtractPdbName(moduleInfo.CVData);
				crashModule.pdbId = FormatPdbId(moduleInfo.PdbSig70, moduleInfo.PdbAge);
				report.modules.push_back(std::move(crashModule));
			}

			SymCleanup(process);
		}

		LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exceptionInfo)
		{
			if (!ClaimCrashHandling())
			{
				return EXCEPTION_EXECUTE_HANDLER;
			}

			CrashReport report;
			report.applicationName = Config().applicationName;

			const DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;

			std::ostringstream reason;
			reason << "Unhandled exception: 0x" << std::hex << code << std::dec
				<< " " << DescribeExceptionCode(code);
			report.reason = reason.str();

			auto addDetail = [&report](const char* label, const DWORD64 value)
			{
				std::ostringstream line;
				line << label << ": 0x" << std::hex << value;
				report.details.push_back(line.str());
			};

			addDetail("Exception address", reinterpret_cast<DWORD64>(exceptionInfo->ExceptionRecord->ExceptionAddress));
			addDetail("Exception flags", exceptionInfo->ExceptionRecord->ExceptionFlags);

			// For an access violation these two say whether it was a read or a write, and at what
			// address — usually the first thing worth knowing.
			if (code == EXCEPTION_ACCESS_VIOLATION && exceptionInfo->ExceptionRecord->NumberParameters >= 2)
			{
				const ULONG_PTR operation = exceptionInfo->ExceptionRecord->ExceptionInformation[0];
				const char* what = (operation == 0) ? "read" : (operation == 1) ? "write" : "execute";
				std::ostringstream line;
				line << "Access violation: " << what << " at 0x" << std::hex
					<< exceptionInfo->ExceptionRecord->ExceptionInformation[1];
				report.details.push_back(line.str());
			}

			addDetail("Rip", exceptionInfo->ContextRecord->Rip);
			addDetail("Rsp", exceptionInfo->ContextRecord->Rsp);
			addDetail("Rbp", exceptionInfo->ContextRecord->Rbp);

			CaptureStack(exceptionInfo->ContextRecord, report);
			FinishReport(report);

			return EXCEPTION_EXECUTE_HANDLER;
		}

		/// abort() - which is where a failed ASSERT ends up - never reaches the unhandled exception
		/// filter, so it needs its own hook. The context is captured here rather than handed to us.
		void OnAbortSignal(int)
		{
			if (!ClaimCrashHandling())
			{
				return;
			}

			CrashReport report;
			report.applicationName = Config().applicationName;
			report.reason = "Aborted (SIGABRT) - usually a failed assertion";

			CONTEXT context = {};
			RtlCaptureContext(&context);
			CaptureStack(&context, report);

			FinishReport(report);
		}
#else
		const char* DescribeSignal(const int signalNumber)
		{
			switch (signalNumber)
			{
			case SIGSEGV:	return "SIGSEGV (invalid memory access)";
			case SIGBUS:	return "SIGBUS (bus error)";
			case SIGFPE:	return "SIGFPE (arithmetic error)";
			case SIGILL:	return "SIGILL (illegal instruction)";
			case SIGABRT:	return "SIGABRT (abort, usually a failed assertion)";
			default:		return "unknown signal";
			}
		}

		void OnFatalSignal(int signalNumber)
		{
			if (!ClaimCrashHandling())
			{
				_exit(EXIT_FAILURE);
			}

			// Capturing the addresses is async-signal-safe. Everything after this point is not:
			// building and writing the report allocates, which can deadlock if the crash happened
			// inside the allocator. That is why the raw trace is dumped to stderr first — it costs
			// nothing and guarantees there is always something to look at.
			void* addresses[64];
			const int frameCount = backtrace(addresses, 64);
			backtrace_symbols_fd(addresses, frameCount, STDERR_FILENO);

			CrashReport report;
			report.applicationName = Config().applicationName;

			std::ostringstream reason;
			reason << "Fatal signal: " << signalNumber << " " << DescribeSignal(signalNumber);
			report.reason = reason.str();

			// Resolve each frame to its module and offset within it. Without this the report would
			// carry only absolute addresses, which are meaningless once the binary is position
			// independent — and tools/symbolicate_crash.ps1 drops any frame lacking module+RVA.
			std::vector<const void*> seenModuleBases;
			for (int i = 0; i < frameCount; ++i)
			{
				CrashFrame frame;
				frame.address = reinterpret_cast<uint64>(addresses[i]);

				Dl_info info = {};
				if (dladdr(addresses[i], &info) != 0 && info.dli_fname != nullptr)
				{
					const std::string path = info.dli_fname;
					const auto slash = path.find_last_of('/');
					frame.moduleName = (slash == std::string::npos) ? path : path.substr(slash + 1);
					frame.moduleRva = reinterpret_cast<uint64>(addresses[i]) - reinterpret_cast<uint64>(info.dli_fbase);

					if (info.dli_sname != nullptr)
					{
						frame.symbolName = info.dli_sname;
					}

					if (std::find(seenModuleBases.begin(), seenModuleBases.end(), info.dli_fbase) == seenModuleBases.end())
					{
						seenModuleBases.push_back(info.dli_fbase);

						CrashModule crashModule;
						crashModule.name = frame.moduleName;
						crashModule.base = reinterpret_cast<uint64>(info.dli_fbase);
						// ELF gives no cheap image size or build id here; the module line still
						// carries the base, which is what makes the RVAs meaningful.
						crashModule.size = 0;
						crashModule.pdbName = frame.moduleName;
						crashModule.pdbId = "elf";
						report.modules.push_back(std::move(crashModule));
					}
				}

				report.frames.push_back(std::move(frame));
			}

			FinishReport(report);

			// Restore the default disposition and re-raise so the exit status still reports the
			// signal and any core dump is produced as configured.
			::signal(signalNumber, SIG_DFL);
			raise(signalNumber);
		}
#endif
	}

	void InstallCrashHandler(CrashHandlerConfig config)
	{
		// A bare log file name has no parent path, which would make create_directories fail
		// silently and drop the report in whatever the working directory happens to be.
		if (config.outputDirectory.empty())
		{
			config.outputDirectory = "logs";
		}

		Config() = std::move(config);

#ifdef _DEBUG
		// Leave first chance to the debugger when one is attached.
#	ifdef _WIN32
		if (IsDebuggerPresent())
		{
			ILOG("Crash handler not installed: a debugger is attached and keeps first chance");
			return;
		}
#	endif
#endif

#ifdef _WIN32
		SetUnhandledExceptionFilter(OnUnhandledException);

		// A failed ASSERT calls abort(), which bypasses the filter above entirely. Route it here
		// and stop the CRT popping a modal dialog, which would hang a headless server rather than
		// letting it die with a report.
		::signal(SIGABRT, OnAbortSignal);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
		// A stack overflow raises SIGSEGV with no usable stack left, so the handler has to run on
		// its own. Without this the one crash class the AI recursion produces cannot be reported.
		// The size is a fixed constant rather than SIGSTKSZ: since glibc 2.34 that macro expands to
		// a sysconf() call, which cannot size an array. 256 KiB clears the runtime minimum on every
		// platform we build for, including AArch64 where SVE inflates it.
		alignas(alignof(std::max_align_t)) static char alternateStack[256 * 1024];
		stack_t signalStack = {};
		signalStack.ss_sp = alternateStack;
		signalStack.ss_size = sizeof(alternateStack);
		signalStack.ss_flags = 0;
		if (sigaltstack(&signalStack, nullptr) != 0)
		{
			// Only costs the stack overflow report, so carry on with the remaining handlers.
			WLOG("Failed to install alternate signal stack, stack overflows will not be reported");
		}

		struct sigaction action = {};
		action.sa_handler = OnFatalSignal;
		sigemptyset(&action.sa_mask);
		action.sa_flags = SA_RESTART | SA_ONSTACK;

		sigaction(SIGSEGV, &action, nullptr);
		sigaction(SIGBUS, &action, nullptr);
		sigaction(SIGFPE, &action, nullptr);
		sigaction(SIGILL, &action, nullptr);
		sigaction(SIGABRT, &action, nullptr);
#endif

		ILOG("Crash handler installed, reports will be written to "
			<< std::filesystem::absolute(Config().outputDirectory).string());
	}

	void UninstallCrashHandler()
	{
		// The onCrash hook captures the owning application object. Once that is going away the hook
		// must go with it, or a fault during shutdown would flush a destroyed log stream.
		Config().onCrash = nullptr;
	}
}
