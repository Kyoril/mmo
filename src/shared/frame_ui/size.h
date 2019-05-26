// Copyright (C) 2019, Robin Klimonow. All rights reserved.


namespace mmo
{
	/// This class is used to render batched ui geometry.
	class Size final
	{
	public:
		static Size Zero;

	public:
		Size(float width = 0.0f, float height = 0.0f) 
			: m_width(width)
			, m_height(height)
		{}

	public:
		float m_width;
		float m_height;
	};
}
