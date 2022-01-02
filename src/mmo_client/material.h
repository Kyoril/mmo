// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <memory>


namespace mmo
{
	/// This class manages material parameters and allows them to be applied to
	/// the graphics pipeline.
	class Material final
	{
	public:
		Material();

	public:
		/// Activates the material, modifying the graphics state accordingly.
		void Set();

	private:

	};


	typedef std::shared_ptr<Material> MaterialPtr;
}
