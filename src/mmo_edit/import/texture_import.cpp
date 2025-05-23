// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <imgui.h>

#include "stb_image/stb_image_resize2.h"

namespace mmo
{
	TextureImport::TextureImport()
	{
	}

	bool TextureImport::ImportFromFile(const Path& filename, const Path& currentAssetPath)
	{
		// Remember these
		m_filesToImport.push_back(filename);
		m_importAssetPath = currentAssetPath;
		m_showImportFileDialog = true;

		return true;
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

	void TextureImport::Draw()
	{
		if (m_showImportFileDialog)
		{
			ImGui::OpenPopup("Texture Import Settings");
			m_showImportFileDialog = false;
		}

		if (ImGui::BeginPopupModal("Texture Import Settings", nullptr))
		{
			ImGui::Text("Importing %d texture files", m_filesToImport.size());
			ImGui::Checkbox("Apply compression", &m_useCompression);

			if (ImGui::Button(m_filesToImport.size() > 1 ? "Import all" : "Import"))
			{
				DoImportInternal();
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Cancel"))
			{
				m_filesToImport.clear();
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	bool TextureImport::DoImportInternal()
	{
		bool succeeded = true;

		for (auto& fileToImport : m_filesToImport)
		{
			int32 width, height, numChannels;
			std::vector<uint8> rawData;
			if (!ReadTextureData(fileToImport, width, height, numChannels, rawData))
			{
				succeeded = false;
				continue;
			}

			TextureData data;
			if (!ConvertData(rawData, width, height, numChannels, data))
			{
				succeeded = false;
				continue;
			}

			// SomeImageFile.jpg => SomeImageFile
			const Path name = fileToImport.filename().replace_extension();
			if (!CreateTextureAsset(name, m_importAssetPath, data))
			{
				ELOG("Failed to import asset " << fileToImport);
				succeeded = false;
			}
		}

		m_filesToImport.clear();

		return succeeded;
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
		std::copy_n(data, width * height * 4, std::back_inserter(rawData));

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
				DLOG("Output format: RGBA");
				return mmo::tex::v1_0::RGBA;
			case mmo::ImageFormat::DXT1:
				DLOG("Output format: DXT1");
				return mmo::tex::v1_0::DXT1;
			case mmo::ImageFormat::DXT5:
				DLOG("Output format: DXT5");
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
				DLOG("Output format: DXT5");
				return mmo::tex::v1_0::DXT5;
			case mmo::ImageFormat::RGBX:
			case mmo::ImageFormat::DXT1:
				DLOG("Output format: DXT1");
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

		// Ensure compression is turned off if not wanted
		if (!m_useCompression)
		{
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

		for (uint32 i = 0; (i < mipCount && i < 16) || i == 0; ++i)
		{
			// After the header, now write the pixel data
			size_t contentPos = sink.Position();
			header.mipmapOffsets[i] = static_cast<uint32>(sink.Position());

			std::vector<uint8> resizedData;
			std::vector<uint8>* dataPtr = &data.data;
			uint16 dataWidth = data.width;
			uint16 dataHeight = data.height;
			if (i > 0)
			{
				// Resize the image
				const uint32 newWidth = std::max(1u, static_cast<uint32>(header.width) >> i);
				const uint32 newHeight = std::max(1u, static_cast<uint32>(header.height) >> i);
				if (newWidth <= 16 && newHeight <= 16)
				{
					break;
				}

				ILOG("Generating mip #" << i << " with size " << newWidth << "x" << newHeight);

				const stbir_pixel_layout pixelLayout = /*data.format == ImageFormat::RGBX ? STBIR_RGB : */STBIR_RGBA;
				const uint32 numChannels = /*data.format == ImageFormat::RGBX ? 3 :*/ 4;

				// Resize the image
				resizedData.resize(newWidth * newHeight * numChannels);
				stbir_resize_uint8_srgb(data.data.data(), header.width, header.height, 
					0, resizedData.data(), newWidth, newHeight, 0, pixelLayout);

				dataPtr = &resizedData;
				dataWidth = newWidth;
				dataHeight = newHeight;
			}

			std::vector<uint8> buffer;

			// Apply compression
			if (applyCompression)
			{
				// Determine if dxt5 is used
				const bool useDxt5 = header.format == v1_0::DXT5;

				// Calculate compressed buffer size
				const size_t compressedSize = useDxt5 ? dataPtr->size() / 4 : dataPtr->size() / 8;

				// Allocate buffer for compression
				buffer.resize(compressedSize);

				// Apply compression
				ILOG("Original size: " << dataPtr->size());
				rygCompress(buffer.data(), dataPtr->data(), dataWidth, dataHeight, useDxt5);
				ILOG("Compressed size: " << buffer.size());

				header.mipmapLengths[i] = static_cast<uint32>(buffer.size());
				sink.Write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
			}
			else
			{
				ILOG("Data size: " << dataPtr->size());
				header.mipmapLengths[i] = static_cast<uint32>(dataPtr->size());
				sink.Write(reinterpret_cast<const char*>(dataPtr->data()), dataPtr->size());
			}
		}
		
		// TODO: Generate mip maps and serialize them as well

		// Finish the header with adjusted data
		saver.finish();

		file->flush();
		return true;
	}
}
