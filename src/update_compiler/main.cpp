#include <iostream>
#include <set>

#include "base/filesystem.h"

#include "cxxopts/cxxopts.hpp"

#include "update_compilation/compile_directory.h"

#include "virtual_dir/file_system_reader.h"
#include "virtual_dir/file_system_writer.h"

int main(int argc, char **argv)
{
	using namespace ::mmo;

	static const std::string VersionStr = "MMORPG Update Compiler 1.0";

	// Paramaters parsed out of command line options
	std::string sourceDir, outputDir;
	std::string compression;

	// Build command line options
	cxxopts::Options options(VersionStr + ", available options");
	options.add_options()
		("h,help", "Produce help message")
		("v,version", "Display the application's name and version")
		("s,source", "A directory containing source.txt", cxxopts::value(sourceDir))
		("o,output", "Where to put the updater-compatible files", cxxopts::value(outputDir))
		("c,compression", "Provide 'zlib' for compression", cxxopts::value(compression))
		;

	// Support positional arguments
	options.parse_positional({ "source", "output" });

	try
	{
		// Parse command line options
		auto results = options.parse(argc, argv);

		if (results.count("version"))
		{
			std::cerr << VersionStr << '\n';
		}

		if (results.count("help"))
		{
			std::cerr << options.help() << "\n";
			return 0;
		}


		bool isZLibCompressed = false;
		if (!compression.empty())
		{
			if (compression == "zlib")
			{
				isZLibCompressed = true;
			}
			else
			{
				std::cerr
					<< "Unknown compression type\n"
					<< "Use 'zlib' for compression\n";
				return 1;
			}
		}

		try
		{
			virtual_dir::FileSystemReader sourceReader(sourceDir);
			virtual_dir::FileSystemWriter outputWriter(outputDir);

			mmo::updating::compileDirectory(
				sourceReader,
				outputWriter,
				isZLibCompressed
			);
		}
		catch (const std::exception &e)
		{
			std::cerr << e.what() << '\n';
			return 1;
		}
	}
	catch (const cxxopts::OptionException& e)
	{
		std::cerr << e.what() << "\n";
	}

	return 0;
}
