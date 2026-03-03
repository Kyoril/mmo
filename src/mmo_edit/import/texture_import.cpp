// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "texture_import.h"

#include <cstring>
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

	bool TextureImport::SupportsExtension(const String& extension) const
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

		if (ImGui::BeginPopupModal("Texture Import Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Importing %d texture file(s)", static_cast<int>(m_filesToImport.size()));
			ImGui::Separator();
			ImGui::Spacing();

			// Texture usage selection
			ImGui::Text("Texture Type:");
			int usage = static_cast<int>(m_textureUsage);
			ImGui::RadioButton("Color (Diffuse, Albedo, UI)", &usage, static_cast<int>(TextureUsage::Color));
			ImGui::RadioButton("Normal Map (XY stored, Z reconstructed)", &usage, static_cast<int>(TextureUsage::NormalMap));
			ImGui::RadioButton("Grayscale (Heightmap, Alpha, Roughness)", &usage, static_cast<int>(TextureUsage::Grayscale));
			m_textureUsage = static_cast<TextureUsage>(usage);

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Compression toggle
			ImGui::Checkbox("Apply compression", &m_useCompression);
			if (m_useCompression)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					switch (m_textureUsage)
					{
					case TextureUsage::Color:
						ImGui::Text("Color: DXT1/BC1 (no alpha) or DXT5/BC3 (with alpha)");
						break;
					case TextureUsage::NormalMap:
						ImGui::Text("Normal Map: BC5 (high quality two-channel compression)");
						break;
					case TextureUsage::Grayscale:
						ImGui::Text("Grayscale: BC4 (single-channel compression)");
						break;
					}
					ImGui::EndTooltip();
				}
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (ImGui::Button(m_filesToImport.size() > 1 ? "Import all" : "Import", ImVec2(120, 0)))
			{
				DoImportInternal();
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Cancel", ImVec2(120, 0)))
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

		switch (m_textureUsage)
		{
		case TextureUsage::Grayscale:
			{
				// Extract only the red channel (or luminance) from the RGBA source
				const size_t pixelCount = static_cast<size_t>(width) * height;
				data.data.resize(pixelCount);
				for (size_t i = 0; i < pixelCount; ++i)
				{
					data.data[i] = rawData[i * 4]; // R channel
				}
				data.format = ImageFormat::R8;
				ILOG("Converted to single-channel grayscale");
			}
			break;

		case TextureUsage::NormalMap:
			{
				// Extract only R and G channels (XY of the normal), Z can be reconstructed in shader
				const size_t pixelCount = static_cast<size_t>(width) * height;
				data.data.resize(pixelCount * 2);
				for (size_t i = 0; i < pixelCount; ++i)
				{
					data.data[i * 2 + 0] = rawData[i * 4 + 0]; // R = X
					data.data[i * 2 + 1] = rawData[i * 4 + 1]; // G = Y
				}
				data.format = ImageFormat::RG8;
				ILOG("Converted to two-channel normal map (XY)");
			}
			break;

		case TextureUsage::Color:
		default:
			{
				if (numChannels == 3)
				{
					data.format = ImageFormat::RGBX;
				}
				else if (numChannels == 4)
				{
					data.format = ImageFormat::RGBA;
				}
				else if (numChannels == 1)
				{
					// Single-channel source but user wants color — expand to RGBA
					data.format = ImageFormat::RGBX;
				}
				else
				{
					ELOG("Unsupported amount of channels in source image data (" << numChannels << ")!");
					return false;
				}
				data.data = rawData;
			}
			break;
		}

		return true;
	}

	/// @brief Determines the output pixel format based on the image format and compression setting.
	static mmo::tex::v1_0::PixelFormat DetermineOutputFormat(const ImageFormat info, const bool compress)
	{
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
			case mmo::ImageFormat::R8:
				DLOG("Output format: R8");
				return mmo::tex::v1_0::R8;
			case mmo::ImageFormat::RG8:
				DLOG("Output format: RG8");
				return mmo::tex::v1_0::RG8;
			}
		}
		else
		{
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
			case mmo::ImageFormat::R8:
				DLOG("Output format: BC4");
				return mmo::tex::v1_0::BC4;
			case mmo::ImageFormat::RG8:
				DLOG("Output format: BC5");
				return mmo::tex::v1_0::BC5;
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

				// Determine channel count and pixel layout based on the source format
				uint32 numChannels;
				stbir_pixel_layout pixelLayout;
				switch (data.format)
				{
				case ImageFormat::R8:
					numChannels = 1;
					pixelLayout = STBIR_1CHANNEL;
					break;
				case ImageFormat::RG8:
					numChannels = 2;
					pixelLayout = STBIR_2CHANNEL;
					break;
				default:
					numChannels = 4;
					pixelLayout = STBIR_RGBA;
					break;
				}

				// Resize the image
				resizedData.resize(newWidth * newHeight * numChannels);
				stbir_resize_uint8_linear(data.data.data(), header.width, header.height, 
					0, resizedData.data(), newWidth, newHeight, 0, pixelLayout);

				dataPtr = &resizedData;
				dataWidth = newWidth;
				dataHeight = newHeight;
			}

			std::vector<uint8> buffer;

			// Apply compression
			if (applyCompression)
			{
				if (header.format == v1_0::BC4)
				{
					// BC4: compress single-channel data
					// We need to expand R8 to RGBA for stb_dxt, then compress as DXT5 and extract alpha block
					// Alternative: write a simple BC4 compressor
					// For now, expand to RGBA and use DXT5, extracting only the alpha blocks
					const size_t pixelCount = static_cast<size_t>(dataWidth) * dataHeight;
					std::vector<uint8> expandedData(pixelCount * 4);
					for (size_t p = 0; p < pixelCount; ++p)
					{
						expandedData[p * 4 + 0] = 0;
						expandedData[p * 4 + 1] = 0;
						expandedData[p * 4 + 2] = 0;
						expandedData[p * 4 + 3] = (*dataPtr)[p]; // Store value in alpha for DXT5 alpha block
					}

					// Compress as DXT5, then extract only the alpha blocks (first 8 bytes of each 16-byte block)
					const size_t dxt5Size = pixelCount; // DXT5 = 1 byte per pixel
					std::vector<uint8> dxt5Buffer(dxt5Size);
					rygCompress(dxt5Buffer.data(), expandedData.data(), dataWidth, dataHeight, true);

					// Extract alpha blocks: each DXT5 block is 16 bytes, alpha block is first 8 bytes
					const size_t numBlocksX = std::max(1, (dataWidth + 3) / 4);
					const size_t numBlocksY = std::max(1, (dataHeight + 3) / 4);
					const size_t totalBlocks = numBlocksX * numBlocksY;
					buffer.resize(totalBlocks * 8);
					for (size_t b = 0; b < totalBlocks; ++b)
					{
						std::memcpy(&buffer[b * 8], &dxt5Buffer[b * 16], 8);
					}

					ILOG("BC4 compressed size: " << buffer.size());
					header.mipmapLengths[i] = static_cast<uint32>(buffer.size());
					sink.Write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
				}
				else if (header.format == v1_0::BC5)
				{
					// BC5: two independent BC4 blocks (one for R, one for G)
					const size_t pixelCount = static_cast<size_t>(dataWidth) * dataHeight;
					const size_t numBlocksX = std::max(1, (dataWidth + 3) / 4);
					const size_t numBlocksY = std::max(1, (dataHeight + 3) / 4);
					const size_t totalBlocks = numBlocksX * numBlocksY;
					buffer.resize(totalBlocks * 16); // 16 bytes per BC5 block

					// Compress R channel via DXT5 alpha
					std::vector<uint8> expandedR(pixelCount * 4, 0);
					for (size_t p = 0; p < pixelCount; ++p)
					{
						expandedR[p * 4 + 3] = (*dataPtr)[p * 2 + 0]; // R in alpha
					}
					std::vector<uint8> dxt5R(pixelCount);
					rygCompress(dxt5R.data(), expandedR.data(), dataWidth, dataHeight, true);

					// Compress G channel via DXT5 alpha
					std::vector<uint8> expandedG(pixelCount * 4, 0);
					for (size_t p = 0; p < pixelCount; ++p)
					{
						expandedG[p * 4 + 3] = (*dataPtr)[p * 2 + 1]; // G in alpha
					}
					std::vector<uint8> dxt5G(pixelCount);
					rygCompress(dxt5G.data(), expandedG.data(), dataWidth, dataHeight, true);

					// Interleave: BC5 block = alpha block from R + alpha block from G
					for (size_t b = 0; b < totalBlocks; ++b)
					{
						std::memcpy(&buffer[b * 16 + 0], &dxt5R[b * 16], 8);  // R block
						std::memcpy(&buffer[b * 16 + 8], &dxt5G[b * 16], 8);  // G block
					}

					ILOG("BC5 compressed size: " << buffer.size());
					header.mipmapLengths[i] = static_cast<uint32>(buffer.size());
					sink.Write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
				}
				else
				{
					// DXT1 or DXT5 compression for standard color textures
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
