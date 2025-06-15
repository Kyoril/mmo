// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "import_base.h"

#include <vector>

namespace mmo
{
	class AssetRegistry;
	
	/// Enumerates available pixel formats for source files.
	enum class ImageFormat
	{
		/// Parser is expected to return 32 bit pixel data, but the alpha channel isn't 
		/// filled with meaningful data and thus discarded. This might be important when
		/// choosing the final pixel format to write.
		RGBX,
		/// 32 bit rgba pixel data with 8 bits per channel. Uncompressed.
		RGBA,
		/// DXT1/BC1 compressed data.
		DXT1,
		/// DXT5/BC3 compressed data.
		DXT5
	};

	/// @brief Implementation for importing textures from files.
	class TextureImport final : public ImportBase
	{
	public:
		explicit TextureImport();
		~TextureImport() override = default;

	public:
		/// @copydoc ImportBase::ImportFromFile 
		bool ImportFromFile(const Path& filename, const Path& currentAssetPath) override;

		/// @copydoc ImportBase::SupportsExtension
		[[nodiscard]] bool SupportsExtension(const String& extension) const override;

		/// Override this method if the importer needs to render some UI elements.
		void Draw() override;

	private:
		bool DoImportInternal();

	private:
		/// Contains infos about a source image file.
		struct TextureData final
		{
			/// Actual width of the image in texels.
			uint16 width;
			/// Actual height of the image in texels.
			uint16 height;
			/// Image texel data format.
			ImageFormat format;
			/// @brief Image data.
			std::vector<uint8> data;
		};

	private:
		/// @brief Reads image information from the given file and extracts the required data.
		/// @param filename The name of the image file to extract data from.
		/// @param width Image width will be stored here.
		/// @param height Image height will be stored here.
		/// @param numChannels Number of channels will be stored here.
		/// @param rawData Raw pixel data will be stored here, in y * x * numChannel order.
		/// @return true on success, false otherwise.
		bool ReadTextureData(const Path& filename, int32& width, int32& height, int32& numChannels, std::vector<uint8>& rawData) const;

		bool ConvertData(const std::vector<uint8>& rawData, int32 width, int32 height, int32 numChannels, TextureData& data);

		/// @brief Creates a texture assets using the given name and path.
		/// @param name Name of the texture file without extension.
		/// @param assetPath The asset path where the texture will be stored.
		/// @return true on success, false otherwise.
		bool CreateTextureAsset(const Path& name, const Path& assetPath, TextureData& data) const;

	private:
		std::vector<Path> m_filesToImport;
		Path m_importAssetPath;
		bool m_showImportFileDialog = false;
		bool m_useCompression = false;
	};
}
