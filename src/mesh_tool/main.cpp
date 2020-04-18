// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#include "cxxopts/cxxopts.hpp"

#include <fstream>
#include <memory>
#include <string>
#include <mutex>

/// String containing the version of this tool.
static const std::string VersionStr = "1.0.0";


/// Procedural entry point of the application.
int main(int argc, char** argv)
{
	// Add cout to the list of log output streams
	std::mutex coutLogMutex;
	mmo::g_DefaultLog.signal().connect([&coutLogMutex](const mmo::LogEntry & entry) 
	{
		std::scoped_lock lock{ coutLogMutex };
		mmo::printLogEntry(std::cout, entry, mmo::g_DefaultConsoleLogOptions);
	});

	// Parameters for source and target file name, eventually filled by command line options
	std::string sourceFile;
	std::string targetFile;

	// Prepare available command line options
	cxxopts::Options options("Mesh Tool " + VersionStr + ", available options");
	options.add_options()
		("help", "produce help message")
		("s,source", "set source file name", cxxopts::value<std::string>(sourceFile))
		("t,target", "set target file name", cxxopts::value<std::string>(targetFile))
		;

	// Add positional parameters
	options.parse_positional({ "source", "target" });

	// Catch exceptions from command line argument parsing. This is a huge try-block because
	// the cxxopts interface has no default constructor for parse results, thus the call to
	// parse needs to stay valid this whole block.
	try
	{
		// Parse command line arguments
		cxxopts::ParseResult result = std::move(options.parse(argc, argv));

		// Check for help output
		if (result.count("help"))
		{
			ILOG(options.help());
			return 0;
		}

		// Check if source file has been set and exists and is readable
		std::ifstream srcFile{ sourceFile.c_str(), std::ios::in | std::ios::binary };
		if (!srcFile || !srcFile.is_open())
		{
			ELOG("Could not read source file " << sourceFile);
			return 1;
		}
		
		// Check if target file has been set
		if (targetFile.empty())
		{
			// Remove extension of source file if there is any and apply the mesh extension
			const size_t extensionDot = sourceFile.rfind('.');
			if (extensionDot != sourceFile.npos) 
			{
				targetFile = sourceFile.substr(0, extensionDot) + ".mesh";
			}
			else
			{
				targetFile = sourceFile + ".mesh";
			}
		}
		else
		{
			// Make sure that the file extension is *.mesh
			const size_t htexExtension = targetFile.rfind(".mesh");
			if (htexExtension == targetFile.npos) 
			{
				targetFile = targetFile + ".mesh";
			}
		}

		// TODO: Load input file


		// Open the output file
		std::ofstream dstFile{ targetFile.c_str(), std::ios::out | std::ios::binary };
		if (!dstFile)
		{
			ELOG("Could not open target file " << targetFile);
			return 1;
		}

		std::vector<uint8> buffer;

		// Write the file
		{
			// TODO
		}
	}
	catch (const cxxopts::OptionException& e)
	{
		// Command line argument parse exception, print it and also the help string
		ELOG(e.what());
		ELOG(options.help());
		return 1;
	}

	return 0;
}
