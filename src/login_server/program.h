// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"

#include <fstream>

namespace mmo
{
	/// This is the main class of the login server application.
	class Program : public NonCopyable
	{
	public:
		/// Runs the application and returns an error code.
		int32 run();

	public:
		/// Set to true to restart the program after successful termination
		static bool ShouldRestart;

	private:
		std::ofstream m_logFile;
	};
}
