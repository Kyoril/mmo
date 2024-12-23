// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "service.h"
#include <stdexcept>
#ifdef __linux__
#	include <unistd.h>
#	include <sys/stat.h>
#endif

namespace mmo
{
	CreateServiceResult createService()
	{
#ifdef __linux__
		{
			const pid_t pid = fork();
			if (pid < 0)
			{
				throw std::runtime_error("fork failed");
			}
			else if (pid != 0)
			{
				// this is the parent process
				return CreateServiceResult::IsObsoleteProcess;
			}
		}
		umask(0);

		{
			const pid_t sid = setsid();
			if (sid < 0)
			{
				throw std::runtime_error("setsid failed");
			}
		}

		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
#endif

		return CreateServiceResult::IsServiceProcess;
	}
}
