// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include <iostream>
#include <fstream>
#include <memory>
#include <string>

#include "tex/pre_header.h"
#include "tex/pre_header_load.h"
#include "tex_v1_0/header.h"
#include "tex_v1_0/header_save.h"
#include "tex_v1_0/header_load.h"
#include "binary_io/stream_sink.h"

#include "cxxopts/cxxopts.hpp"

#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"


/// String containing the version of this tool.
static const std::string VersionStr = "1.0.0";


namespace mmo
{
	enum class ImageFormat
	{
		RGB,
		RGBA,
		DXT1,
		DXT3,
		DXT5
	};

	/// This interface is the base class for parsing an image file.
	struct IImageParser
	{
		virtual ~IImageParser() = default;

		/// Parses image data.
		virtual bool Parse(std::istream& data, uint16& width, uint16& height, ImageFormat& format, std::vector<uint8>& pixels) = 0;
	};

#ifndef LOWORD
#define LOWORD(l)           ((uint16)(((uint32)(l)) & 0xffff))
#endif
#ifndef HIWORD
#define HIWORD(l)           ((uint16)((((uint32)(l)) >> 16) & 0xffff))
#endif

	/// This class parses png image data.
	struct TgaImageParser
		: IImageParser
	{
#pragma pack(push, 1)
		struct TargaHeader
		{
			uint8 IDLength;
			uint8 ColorMapType;
			uint8 DataTypeCode;
			uint16 wColorMapOrigin;
			uint16 wColorMapLength;
			uint8 ColorMapDepth;
			uint16 wOriginX;
			uint16 wOriginY;
			uint16 wWidth;
			uint16 wHeight;
			uint8 BitDepth;
			uint8 ImageDescriptor;
		};
#pragma pack(pop)

		virtual bool Parse(std::istream& data, uint16& width, uint16& height, ImageFormat& format, std::vector<uint8>& pixels) override
		{
			// Setup source and  reader
			io::StreamSource source{ data };
			io::Reader reader{ source };

			// Read targa header
			TargaHeader header;
			reader.readPOD(header);
			if (!reader)
				return false;

			// We only support uncompressed tga
			if (header.DataTypeCode != 2)
				return false;

			// We don't support color palettes
			if (header.ColorMapType != 0)
				return false;

			// We only support 24 or 32 bits
			if (header.BitDepth != 24 && header.BitDepth != 32)
				return false;

			// Skip image id data
			if (header.IDLength > 0)
				reader.skip(header.IDLength);

			// Check if still valid
			if (!reader)
				return false;

			// Write image size
			width = header.wWidth;
			height = header.wHeight;

			// Write image format
			format = ImageFormat::RGBA;

			// Image data: BGR(A)
			pixels.resize(width * height * 4, 0xff);
			for (uint32 y = 0; y < height; ++y)
			{
				for (uint32 x = 0; x < width ; ++x)
				{
					uint8 b = 0, g = 0, r = 0, a = 0xff;
					reader >> io::read<uint8>(b) >> io::read<uint8>(g) >> io::read<uint8>(r);
					if (header.BitDepth == 32)
						reader >> io::read<uint8>(a);

					if (!reader)
						return false;

					const size_t index = x + y * width;
					pixels[index * 4 + 0] = r;
					pixels[index * 4 + 1] = g;
					pixels[index * 4 + 2] = b;
					pixels[index * 4 + 3] = a;
				}
				
			}

			return true;
		}
	};
}



/// Procedural entry point of the application.
int main(int argc, char** argv)
{
	std::string sourceFile;
	std::string targetFile;

	// Prepare available command line options
	cxxopts::Options options("Texture Tool " + VersionStr + ", available options");
	options.add_options()
		("help", "produce help message")
		("i,info", "describes the htex source file")
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
		if (!srcFile || !srcFile.is_open())
		{
			std::cerr << "Could not read source file " << sourceFile << "\n";
			return 1;
		}

		// If we just want to get info about a given file, load it and print the info
		if (result.count("info"))
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
				std::cerr << "Failed to read htex pre header! File might be damaged\n";
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
					std::cerr << "Failed to read the v1.0 header! The file might be damaged\n";
					return 1;
				}

				// Describe the header file
				std::cout << "Size: " << header.width << "x" << header.height << "\n";
				std::cout << "Has mip maps: " << (header.hasMips ? "true" : "false") << "\n";
				std::cout << "Compression: " << header.compression << "\n";
				std::cout << "Mip map infos:\n";
				for (size_t i = 0; i < header.mipmapOffsets.size(); ++i)
				{
					std::cout << "\t#" << i << ":\tOffset " << header.mipmapOffsets[i] << ";\tLength: " << header.mipmapLengths[i] << "\n";
				}
			} break;

			default:
				std::cerr << "Unsupported htex version" << preHeader.version << "\n";
				return 1;
			}
		}
		else
		{
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

			// Parse in the source data and determine parameters
			std::unique_ptr<mmo::IImageParser> imageParser;
			imageParser = std::make_unique<mmo::TgaImageParser>();

			uint16 width = 0, height = 0;
			mmo::ImageFormat format;
			std::vector<uint8> pixelData;
			if (!imageParser->Parse(srcFile, width, height, format, pixelData))
			{
				std::cerr << "Failed to parse source image file!\n";
				return 1;
			}

			// Open the output file
			std::ofstream dstFile{ targetFile.c_str(), std::ios::out | std::ios::binary };
			if (!dstFile)
			{
				std::cerr << "Could not open target file " << targetFile << "\n";
				return 1;
			}

			// Write the file
			{
				using namespace mmo::tex;

				// Generate writer
				io::StreamSink sink{ dstFile };
				io::Writer writer{ sink };

				// Initialize the header
				v1_0::Header header{ Version_1_0 };
				header.width = width;
				header.height = height;
				header.compression = v1_0::NotCompressed;
				std::cout << "Image size: " << width << "x" << height << "\n";

				// Check if support for mipmapping is enabled
				bool widthIsPow2 = false, heightIsPow2 = false;
				uint32 mipCount = 0;
				for (uint32 i = 0; i < 16; ++i)
				{
					if (header.width == (1 << i)) {
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
				std::cout << "Image supports mip maps: " << (header.hasMips ? "true" : "false") << "\n";
				if (header.hasMips)
				{
					std::cout << "Number of mip maps: " << mipCount;
				}

				// Generate a sink to save the header
				v1_0::HeaderSaver saver{ sink, header };

				// After the header, now write the pixel data
				size_t contentPos = sink.position();
				header.mipmapOffsets[0] = static_cast<uint32>(sink.position());
				header.mipmapLengths[0] = static_cast<uint32>(pixelData.size());
				sink.write(reinterpret_cast<const char*>(pixelData.data()), pixelData.size());

				// Finish the header with adjusted data
				saver.finish();
			}
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
