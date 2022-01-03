// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "image_parser.h"


namespace mmo
{
	/// This class parses png image data.
	class PngImageParser final : public IImageParser
	{
	public:
		// ~Begin IImageParser
		bool Parse(std::istream& data, SourceImageInfo& out_info, ImageData& out_data) override;
		// ~End IImageParser
	};
}
