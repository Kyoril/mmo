#include "create_process.h"

#if defined(__unix__) || defined(__APPLE__)
#	include <spawn.h>
#	include <sys/stat.h>
#	include <cstring>
#	include <iostream>
#	include <exception>
#elif defined _WIN32
#	include <Windows.h>
#endif

#include <stdexcept>


namespace mmo
{
	void createProcess(
	    std::string executable,
	    std::vector<std::string> arguments)
	{
#ifdef __unix__
		std::vector<char *> convertedArguments;
		convertedArguments.push_back(&executable.front());
		for (size_t i = 0; i < arguments.size(); ++i)
		{
			convertedArguments.push_back(&arguments[i].front());
		}
		convertedArguments.push_back(nullptr);

		posix_spawn_file_actions_t fileActions;
		std::memset(&fileActions, 0, sizeof(fileActions));

		pid_t processId;

		const int result = posix_spawn(
		                       &processId,
		                       executable.c_str(),
		                       &fileActions,
		                       nullptr,
		                       convertedArguments.data(),
		                       nullptr);
		if (result == 0)
		{
			std::cerr << "Launched updater with process id " << processId << '\n';
		}
		else
		{
			std::cerr << "Could not launch updater, errno: " << errno << '\n';
		}
#endif

#ifdef _WIN32
		std::string commandLine = "\"" + executable + "\"";//
		for (auto const& s : arguments) { commandLine += " " + s; }

		if ((commandLine.length() + 1) > 32768)
		{
			throw std::runtime_error(
			    "Command line is too long for CreateProcess");
		}

		STARTUPINFO info;
		std::memset(&info, 0, sizeof(info));
		info.cb = sizeof(info);
		PROCESS_INFORMATION ignored;
		if (!CreateProcessA(
		            executable.c_str(),
		            &commandLine[0],
		            nullptr,
		            nullptr,
		            FALSE,
		            0,
		            nullptr,
		            nullptr,
		            &info,
		            &ignored))
		{
			throw std::runtime_error("Failed to spawn process! Last error: " +
				std::to_string(::GetLastError()));
		}
		::CloseHandle(ignored.hThread);
		::CloseHandle(ignored.hProcess);
#endif
	}

	void makeExecutable(const std::string &file)
	{
#ifdef __unix__
		struct stat s;
		int result = stat(file.c_str(), &s);
		if (result == 0)
		{
			result = chmod(file.c_str(), s.st_mode | 0100);
			if (result == 0)
			{
				return;
			}
		}

		throw std::runtime_error("Failed to create executable! Result: " +
			std::to_string(result));
#else
		(void)file;
#endif
	}
}
