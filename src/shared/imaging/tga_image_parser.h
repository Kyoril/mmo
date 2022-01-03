// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "image_parser.h"


namespace mmo
{
	/// This class parses tga image data.
	class TgaImageParser final : public IImageParser
	{
	public:
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

	public:
		// ~Begin IImageParser
		bool Parse(std::istream& data, SourceImageInfo& out_info, ImageData& out_data) override;
		// ~End IImageParser
	};
}
