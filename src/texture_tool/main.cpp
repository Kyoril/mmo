// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "imaging/tga_image_parser.h"
#include "imaging/png_image_parser.h"
#include "tex/pre_header.h"
#include "tex/pre_header_load.h"
#include "tex_v1_0/header.h"
#include "tex_v1_0/header_save.h"
#include "tex_v1_0/header_load.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"
#include "log/log_std_stream.h"

#include "cxxopts/cxxopts.hpp"

#define STB_DXT_IMPLEMENTATION
#include <filesystem>

#include "stb_dxt.h"

#include <fstream>
#include <memory>
#include <string>
#include <mutex>

/// String containing the version of this tool.
static const std::string VersionStr = "1.2.0";


namespace
{
	/// Shows the application info
	static int32 ShowInfo(std::istream& srcFile)
	{
		// Read the source file to print out informations about it
		using namespace mmo::tex;

		// Open reader on source file
		io::StreamSource source{ srcFile };
		io::Reader reader{ source };

		// Load the pre header
		PreHeader preHeader;
		if (!loadPreHeader(preHeader, reader))
		{
			ELOG("Failed to read htex pre header! File might be damaged");
			return 1;
		}

		// Check version
		switch (preHeader.version)
		{
		case Version_1_0:
		{
			// Load the header
			v1_0::Header header{ preHeader.version };
			if (!v1_0::loadHeader(header, reader))
			{
				ELOG("Failed to read the v1.0 header! The file might be damaged");
				return 1;
			}

			// Describe the header file
			ILOG("Size: " << header.width << "x" << header.height);
			ILOG("Has mip maps: " << (header.hasMips ? "true" : "false"));
			ILOG("Format: " << FormatDescription(header.format));
			ILOG("Mip map infos:");
			for (size_t i = 0; i < header.mipmapOffsets.size(); ++i)
			{
				ILOG("\t#" << i << ":\tOffset " << header.mipmapOffsets[i] << ";\tLength: " << header.mipmapLengths[i]);
			}
		} break;

		default:
			ELOG("Unsupported htex version" << preHeader.version);
			return 1;
		}

		return 0;
	}

	/// Determines the output pixel format.
	static mmo::tex::v1_0::PixelFormat DetermineOutputFormat(const mmo::SourceImageInfo& info, const bool compress)
	{
		// Determine output pixel format
		if (!compress)
		{
			switch (info.format)
			{
			case mmo::ImageFormat::RGBX:
			case mmo::ImageFormat::RGBA:
				return mmo::tex::v1_0::RGBA;
			case mmo::ImageFormat::DXT1:
				return mmo::tex::v1_0::DXT1;
			case mmo::ImageFormat::DXT5:
				return mmo::tex::v1_0::DXT5;
			default:
				UNREACHABLE();
				break;
			}
		}
		else
		{
			// If there is an alpha channel in the source format, we need to apply DXT5
			// since DXT1 does not support alpha channels
			switch (info.format)
			{
			case mmo::ImageFormat::RGBA:
			case mmo::ImageFormat::DXT5:
				return mmo::tex::v1_0::DXT5;
			case mmo::ImageFormat::RGBX:
			case mmo::ImageFormat::DXT1:
				return mmo::tex::v1_0::DXT1;
			default:
				UNREACHABLE();
				break;
			}
		}

		// Unknown / unsupported pixel format
		return mmo::tex::v1_0::Unknown;
	}
}


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
	cxxopts::Options options("Texture Tool " + VersionStr + ", available options");
	options.add_options()
		("help", "produce help message")
		("i,info", "describes the htex source file")
		("s,source", "set source file name", cxxopts::value<std::string>(sourceFile))
		("t,target", "set target file name", cxxopts::value<std::string>(targetFile))
		("r,raw", "disable compression for the output file")
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

		// Check for compression
		bool compress = (result.count("raw")) == 0;

		// Check if source file has been set and exists and is readable
		std::ifstream srcFile{ sourceFile.c_str(), std::ios::in | std::ios::binary };
		if (!srcFile || !srcFile.is_open())
		{
			ELOG("Could not read source file " << sourceFile);
			return 1;
		}

		// If we just want to get info about a given file, load it and print the info
		if (result.count("info"))
		{
			return ShowInfo(srcFile);
		}

		// Check if target file has been set
		if (targetFile.empty())
		{
			// Remove extension of source file if there is any and apply the htex extension
			const size_t extensionDot = sourceFile.rfind('.');
			if (extensionDot != sourceFile.npos) 
			{
				targetFile = sourceFile.substr(0, extensionDot) + ".htex";
			}
			else
			{
				targetFile = sourceFile + ".htex";
			}
		}
		else
		{
			// Make sure that the file extension is *.htex
			const size_t htexExtension = targetFile.rfind(".htex");
			if (htexExtension == targetFile.npos) 
			{
				targetFile = targetFile + ".htex";
			}
		}

		const auto sourcePath = std::filesystem::path(sourceFile);
		const auto extension = sourcePath.extension().string();
		
		std::unique_ptr<mmo::IImageParser> imageParser;
		if (extension == ".png")
		{
			ILOG("Using PNG image parser");
			imageParser = std::make_unique<mmo::PngImageParser>();
		}
		else if(extension == ".tga")
		{
			ILOG("Using TGA image parser");
			imageParser = std::make_unique<mmo::TgaImageParser>();
		}

		if (!imageParser)
		{
			ELOG("Unsupported source file extension!");
			return 1;
		}

		// Parse image information
		mmo::SourceImageInfo info;
		mmo::ImageData pixelData;
		if (!imageParser->Parse(srcFile, info, pixelData))
		{
			ELOG("Failed to parse source image file!");
			return 1;
		}
		
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
			using namespace mmo::tex;

			// Generate writer
			io::StreamSink sink{ dstFile };
			io::Writer writer{ sink };

			// Initialize the header
			v1_0::Header header{ Version_1_0 };
			header.width = info.width;
			header.height = info.height;
			ILOG("Image size: " << info.width << "x" << info.height);

			// Check for correct image size since DXT requires a multiple of 4 as image size
			if (compress)
			{
				if ((info.width % 4) != 0 || (info.height % 4) != 0)
				{
					WLOG("DXT compression requires that both the width and height of the source image have to be a multiple of 4! Compression is disabled...");
					compress = false;
				}
			}

			// Determine output format
			header.format = DetermineOutputFormat(info, compress);

			// Check if support for mipmapping is enabled
			bool widthIsPow2 = false, heightIsPow2 = false;
			uint32 mipCount = 0;
			for (uint32 i = 0; i < 16; ++i)
			{
				if (header.width == (1 << i)) 
				{
					widthIsPow2 = true;

					if (mipCount == 0) mipCount = i + 1;
				}
				if (header.height == (1 << i))
				{
					heightIsPow2 = true;
					if (mipCount == 0) mipCount = i + 1;
				}
			}

			// Check the number of mip maps
			header.hasMips = widthIsPow2 && heightIsPow2;
			ILOG("Image supports mip maps: " << (header.hasMips ? "true" : "false"));
			if (header.hasMips)
			{
				ILOG("Number of mip maps: " << mipCount);
			}

			// Generate a sink to save the header
			v1_0::HeaderSaver saver{ sink, header };

			// After the header, now write the pixel data
			size_t contentPos = sink.Position();
			header.mipmapOffsets[0] = static_cast<uint32>(sink.Position());

			// Apply compression
			if (compress)
			{
				// Determine if dxt5 is used
				const bool useDxt5 = header.format == v1_0::DXT5;

				// Calculate compressed buffer size
				const size_t compressedSize = useDxt5 ? pixelData.size() / 4 : pixelData.size() / 8;

				// Allocate buffer for compression
				buffer.resize(compressedSize);

				// Apply compression
				ILOG("Original size: " << pixelData.size());
				rygCompress(&buffer[0], &pixelData[0], info.width, info.height, useDxt5);
				ILOG("Compressed size: " << buffer.size());

				header.mipmapLengths[0] = static_cast<uint32>(buffer.size());
				sink.Write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
			}
			else
			{
				header.mipmapLengths[0] = static_cast<uint32>(pixelData.size());
				sink.Write(reinterpret_cast<const char*>(pixelData.data()), pixelData.size());
			}

			// TODO: Generate mip maps and serialize them as well

			// Finish the header with adjusted data
			saver.finish();
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
