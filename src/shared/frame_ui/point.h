// Copyright (C) 2019, Robin Klimonow. All rights reserved.


namespace mmo
{
	/// This class is used to render batched ui geometry.
	class Point final
	{
	public:
		static Point Zero;

	public:
		Point(float x = 0.0f, float y = 0.0f)
			: m_x(x)
			, m_y(y)
		{
		}

	public:
		float m_x, m_y;
	};
}
