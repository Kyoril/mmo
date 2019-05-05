// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"

namespace mmo
{
	/// This is the base class of a graphics device object.
	class GraphicsDevice
		: public NonCopyable
	{
	protected:
		// Protected default constructor.
		GraphicsDevice();

	public:
		/// Virtual default destructor.
		virtual ~GraphicsDevice() = default;

	public:
#ifdef _WIN32
		/// Creates a new d3d11 graphics device object and makes it the current one.
		static GraphicsDevice& CreateD3D11();
#endif
		/// Gets the current graphics device object. Will throw an exception if there
		/// is no graphics device at the time.
		static GraphicsDevice& Get();
		/// Destroys the current graphics device object if there is one.
		static void Destroy();
	};
}
