// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once


namespace mmo
{
	/// This is the base class of a texture.
	class Texture
	{
	public:
		/// Virtual default destructor because of inheritance.
		virtual ~Texture() = default;
	};
}
