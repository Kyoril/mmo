// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "texture_import.h"

#include <set>

#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "log/default_log_levels.h"

#include "writer.h"

#include "tex_v1_0/header.h"
#include "tex_v1_0/header_save.h"

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb_image.h"

namespace mmo
{
	TextureImport::TextureImport()
	{
	}

	bool TextureImport::ImportFromFile(const Path& filename, const Path& currentAssetPath)
	{
		int32 width, height, numChannels;
		std::vector<uint8> rawData;
		if (!ReadTextureData(filename, width, height, numChannels, rawData))
		{
			return false;
		}
		
		TextureData data;
		if (!ConvertData(rawData, width, height, numChannels, data))
		{
			return false;
		}
		
		// SomeImageFile.jpg => SomeImageFile
		const Path name = filename.filename().replace_extension();
		return CreateTextureAsset(name, currentAssetPath, data);
	}

	bool TextureImport::SupportsExtension(const String& extension) const noexcept
	{
		// Use formats supported by stb_image implementation
		static const std::set<String> supportedExtension = {
			".png",
			".jpg",
			".psd",
			".tga",
			".bmp"
		};

		return supportedExtension.contains(extension);
	}

	bool TextureImport::ReadTextureData(const Path& filename, int32& width, int32& height, int32& numChannels, std::vector<uint8>& rawData) const
	{
		int x,y,n;
		uint8* data = stbi_load(filename.string().c_str(), &x, &y, &n, 4);
		if (!data)
		{
			ELOG("Unable to read source image file, maybe file is damaged or format is not supported!");
			return false;
		}

		width = x;
		height = y;
		numChannels = n;

		rawData.reserve(width * height * 4);
		std::copy(data, data + width * height * 4, std::back_inserter(rawData));

		stbi_image_free(data);
		return true;
	}

	bool TextureImport::ConvertData(const std::vector<uint8>& rawData, const int32 width, const int32 height, const int32 numChannels, TextureData& data)
	{
		if (width <= 0 || width > std::numeric_limits<uint16>::max()) 
		{
			ELOG("Unsupported width value (" << width << ") of source image data, has to be in range of 1.." << std::numeric_limits<uint16>::max());
			return false;
		}

		if (height <= 0 || height > std::numeric_limits<uint16>::max()) 
		{
			ELOG("Unsupported height value (" << height << ") of source image data, has to be in range of 1.." << std::numeric_limits<uint16>::max());
			return false;
		}

		data.width = static_cast<uint16>(width);
		data.height = static_cast<uint16>(height);

		if (numChannels == 3)
		{
			data.format = ImageFormat::RGBX;
		}
		else if (numChannels == 4)
		{
			data.format = ImageFormat::RGBA;
		}
		else
		{
			ELOG("Unsupported amount of channels in source image data (" << numChannels << "), only 3 or 4 channels are supported!");
			return false;
		}

		data.data = rawData;
		return true;
	}

	/// @brief Determines the output pixel format.
	static mmo::tex::v1_0::PixelFormat DetermineOutputFormat(const ImageFormat info, const bool compress)
	{
		// Determine output pixel format
		if (!compress)
		{
			switch (info)
			{
			case mmo::ImageFormat::RGBX:
			case mmo::ImageFormat::RGBA:
				return mmo::tex::v1_0::RGBA;
			case mmo::ImageFormat::DXT1:
				return mmo::tex::v1_0::DXT1;
			case mmo::ImageFormat::DXT5:
				return mmo::tex::v1_0::DXT5;
			}
		}
		else
		{
			// If there is an alpha channel in the source format, we need to apply DXT5
			// since DXT1 does not support alpha channels
			switch (info)
			{
			case mmo::ImageFormat::RGBA:
			case mmo::ImageFormat::DXT5:
				return mmo::tex::v1_0::DXT5;
			case mmo::ImageFormat::RGBX:
			case mmo::ImageFormat::DXT1:
				return mmo::tex::v1_0::DXT1;
			}
		}

		// Unknown / unsupported pixel format
		return mmo::tex::v1_0::Unknown;
	}

	bool TextureImport::CreateTextureAsset(const Path& name, const Path& assetPath, TextureData& data) const
	{
		const String filename = (assetPath / name).string() + ".htex";

		const auto file = AssetRegistry::CreateNewFile(filename);
		if (!file)
		{
			ELOG("Unable to create new asset file " << filename);
			return false;
		}

		using namespace mmo::tex;

		// Generate writer
		io::StreamSink sink{ *file };
		io::Writer writer{ sink };

		// Initialize the header
		v1_0::Header header{ Version_1_0 };
		header.width = data.width;
		header.height = data.height;
		ILOG("Image size: " << data.width << "x" << data.height);

		// Check for correct image size since DXT requires a multiple of 4 as image size
		bool applyCompression = true;
		if ((data.width % 4) != 0 || (data.height % 4) != 0)
		{
			WLOG("DXT compression requires that both the width and height of the source image have to be a multiple of 4! Compression is disabled...");
			applyCompression = false;
		}

		// Determine output format
		header.format = DetermineOutputFormat(data.format, applyCompression);

		// Check if support for mip mapping is enabled
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
		
		std::vector<uint8> buffer;

		// Apply compression
		if (applyCompression)
		{
			// Determine if dxt5 is used
			const bool useDxt5 = header.format == v1_0::DXT5;

			// Calculate compressed buffer size
			const size_t compressedSize = useDxt5 ? data.data.size() / 4 : data.data.size() / 8;

			// Allocate buffer for compression
			buffer.resize(compressedSize);

			// Apply compression
			ILOG("Original size: " << data.data.size());
			rygCompress(&buffer[0], &data.data[0], data.width, data.height, useDxt5);
			ILOG("Compressed size: " << buffer.size());

			header.mipmapLengths[0] = static_cast<uint32>(buffer.size());
			sink.Write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
		}
		else
		{
			ILOG("Data size: " << data.data.size());
			header.mipmapLengths[0] = static_cast<uint32>(data.data.size());
			sink.Write(reinterpret_cast<const char*>(data.data.data()), data.data.size());
		}

		// TODO: Generate mip maps and serialize them as well

		// Finish the header with adjusted data
		saver.finish();

		file->flush();
		return true;
	}
}
