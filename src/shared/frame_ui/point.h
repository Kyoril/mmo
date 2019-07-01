// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once


namespace mmo
{
	/// This class is used to render batched ui geometry.
	class Point final
	{
	public:
		static Point Zero;

	public:
		Point(float x = 0.0f, float y = 0.0f)
			: x(x)
			, y(y)
		{
		}

	public:
		float x, y;
	};
}
