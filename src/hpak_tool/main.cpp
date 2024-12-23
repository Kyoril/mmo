// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "base/typedefs.h"

#include "extract.h"
#include "pack.h"
#include "describe.h"

#include <iostream>
#include <iomanip>
#include <fstream>

#include "cxxopts/cxxopts.hpp"


using namespace mmo;
using namespace std;


/// String containing the version of this tool.
static const std::string VersionStr = "1.0.0";


namespace
{
	/// Simple callback for extraction which will print the progress to std::cout
	void DisplayFileProgress(double totalProgress, const std::filesystem::path &currentFile)
	{
		const auto percentage = min<double>(max<double>(totalProgress, 0), 1) * 100;
		cout
		        << std::setw(2) << static_cast<unsigned>(percentage) << "%  "
		        << currentFile << "\n";
	}
}

/// Procedural entry point of the application.
int main(int argc, char **argv)
{
	String fileName;
	String directoryPath;

	// Prepare available command line options
	cxxopts::Options options("HPAK Tool " + VersionStr + ", available options");
	options.add_options()
		("help", "produce help message")
		("f,file", "set archive file name", cxxopts::value<std::string>(fileName))
		("d,dir", "set target directory", cxxopts::value<std::string>(directoryPath))
		("e,extract", "extract the file into the directory")
		("p,pack", "pack the directory into the file")
		("r,raw", "do not zlib compress files when packing")
		("describe", "print archive description to stdout")
		;

	// Add positional parameters to allow for something like this:
	//	hpak_tool.exe -e file dir
	// instead of
	//	hpak_tool.exe -e -f file -d dir
	options.parse_positional({ "file", "dir" });

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
			cerr << options.help() << "\n";
		}

		// If the user wants to extract the given archive, we will do so
		if (result.count("extract"))
		{
			// Try to open the archive file for reading
			ifstream archive(fileName, std::ios::binary);
			if (!archive)
			{
				cerr << "Could not open archive " << fileName << "\n";
				return 1;
			}

			// Extract the archive
			const bool success = Extract(
				archive,
				directoryPath,
				DisplayFileProgress);

			// Report back success or error
			if (success)
			{
				cerr << "Archive extracted\n";
				return 0;
			}
			else
			{
				cerr << "Archive not extracted!\n";
				return 1;
			}
		}
		else if (result.count("pack"))
		{
			// Create the new archive file for writing. This will overwrite an existing archive file if there is one!
			ofstream archive(fileName, std::ios::binary);
			if (!archive)
			{
				cerr << "Could not open archive " << fileName << "\n";
				return 1;
			}

			// Inclusion filter to avoid packing the newly generated file
			auto filter = [&fileName](const std::filesystem::path & file)
			{
				return !std::filesystem::equivalent(fileName, file);
			};

			// If the raw argument is set, we will not compress archive file contents.
			const bool isCompressionEnabled = (result.count("raw") == 0);

			// Pack contents of the given folder
			const bool success = Pack(archive,
				directoryPath,
				isCompressionEnabled,
				std::move(filter),
				DisplayFileProgress);

			// Report back success
			if (success)
			{
				cerr << "Directory packed\n";
				return 0;
			}
			else
			{
				cerr << "Directory not packed!\n";
				return 1;
			}
		}
		else if (result.count("describe"))
		{
			// Open the given archive file for reading
			std::ifstream archive(fileName, std::ios::binary);
			if (!archive)
			{
				cerr << "Could not open archive " << fileName << "\n";
				return 1;
			}

			try
			{
				// Describe the archive contents and write them to cout
				Describe(archive, std::cout);
			}
			catch (const std::exception &ex)
			{
				std::cerr << ex.what() << '\n';
				return 1;
			}
			catch (...)
			{
				std::cerr << "Unknown error\n";
				return 1;
			}
		}
	}
	catch (const cxxopts::OptionException& e)
	{
		// Command line argument parse exception, print it and also the help string
		cerr << e.what() << "\n\n";
		cerr << options.help() << "\n";
		return 1;
	}

}
