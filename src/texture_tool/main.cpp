// Copyright (C) 2019, Robin Klimonow. All rights reserved.


#include <iostream>
#include <fstream>
#include <string>

#include "tex_v1_0/header.h"
#include "tex_v1_0/header_save.h"
#include "binary_io/stream_sink.h"

#include "cxxopts/cxxopts.hpp"


/// String containing the version of this tool.
static const std::string VersionStr = "1.0.0";


/// Procedural entry point of the application.
int main(int argc, char** argv)
{
	std::string sourceFile;
	std::string targetFile;

	// Prepare available command line options
	cxxopts::Options options("Texture Tool " + VersionStr + ", available options");
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
			std::cerr << options.help() << "\n";
		}

		// Check if source file has been set and exists and is readable
		std::ifstream srcFile{ sourceFile.c_str(), std::ios::in | std::ios::binary };
		if (!srcFile)
		{
			std::cerr << "Could not read source file " << sourceFile << "\n";
			return 1;
		}

		// Check if target file has been set
		if (targetFile.empty())
		{
			// Remove extension of source file
			size_t extensionDot = sourceFile.rfind('.');
			if (extensionDot != sourceFile.npos) {
				targetFile = sourceFile.substr(0, extensionDot) + ".htex";
			}
		}
		else
		{
			// Make sure that the file extension is *.htex
			size_t htexExtension = targetFile.rfind(".htex");
			if (htexExtension == targetFile.npos) {
				targetFile = targetFile + ".htex";
			}
		}

		// Open the output file
		std::ofstream dstFile{ targetFile.c_str(), std::ios::out | std::ios::binary };
		if (!dstFile)
		{
			std::cerr << "Could not open target file " << targetFile << "\n";
			return 1;
		}

		// Parse in the source data and determine parameters

		// Do the thing!
		{
			using namespace mmo::tex;

			// Setup the header
			v1_0::Header header{ VersionId::Version_1_0 };

			// Generate a sink to save the header
			io::StreamSink sink{ dstFile };
			v1_0::HeaderSaver saver{ sink, header };
		}
	}
	catch (const cxxopts::OptionException& e)
	{
		// Command line argument parse exception, print it and also the help string
		std::cerr << e.what() << "\n\n";
		std::cerr << options.help() << "\n";
		return 1;
	}

	return 0;
}
