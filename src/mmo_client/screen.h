// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

// This file contains the screen system. The screen system is a system which hooks into the
// event loops Paint event. It allows to draw things in order and should also simplify the 
// use of often-required graphics pipeline changes by the use of simple flags.
// For example, before each layer is drawn, the transformation matrices are set up properly
// as specified by the flags.
// To draw stuff above the screen, you can still use the Paint event in the event loop, since
// the screen system is most likely the first system to be initialized.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include <functional>
#include <list>


namespace mmo
{
	/// Enumerates possible screen layer flags.
	enum ScreenLayerFlags
	{
		/// The screen layer is rendered using screen space only.
		IdentityProjection = 1,
		/// The screen layer is disabled (currently not drawn).
		Disabled = 2,
		/// The screen layer is rendered using screen space only.
		IdentityTransform = 4,
	};

	/// Function callback that is executed once a layer should be drawn.
	typedef std::function<void()> ScreenLayerPaintFunc;

	/// This struct contains data of a screen layer.
	struct ScreenLayer
	{
		/// A paint function callback used to draw the screen layer.
		ScreenLayerPaintFunc paintFunc;
		/// The layer priority
		float priority = 1.0f;
		/// Possible screen layer flags.
		uint32 flags = 0;
	};

	// Screen layer iterator returned by AddLayer. Used to remove layers manually later on or
	// modify layer properties on the fly.
	typedef std::list<ScreenLayer>::iterator ScreenLayerIt;

	/// This class manages the screen and allows to register a new screen layer which can
	/// be used for drawing.
	class Screen final
		: public NonCopyable
	{
	private:
		/// Private default constructor to avoid instantiation.
		Screen() = default;

	public:
		/// Initializes the screen system.
		static void Initialize();
		/// Destroys the screen system.
		static void Destroy();

		/// Registers a new screen layer.
		static ScreenLayerIt AddLayer(ScreenLayerPaintFunc paintFunc, float priority, uint32 flags);
		/// Removes an existing screen layer.
		static void RemoveLayer(ScreenLayerIt& layer);
	};
}
