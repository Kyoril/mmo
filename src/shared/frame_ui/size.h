// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once


namespace mmo
{
	/// This class is used to render batched ui geometry.
	class Size final
	{
	public:
		static Size Zero;

	public:
		Size(float width = 0.0f, float height = 0.0f) 
			: width(width)
			, height(height)
		{}

	public:
		inline bool operator==(const Size& b) const noexcept
		{
			return width == b.width && height == b.height;
		}
		inline bool operator!=(const Size& b) const noexcept
		{
			return !(*this == b);
		}

	public:
		float width;
		float height;
	};
}
