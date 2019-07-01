// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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
		float width;
		float height;
	};
}
