// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "screen.h"
#include "event_loop.h"

#include "base/macros.h"
#include "graphics/graphics_device.h"

#include <list>
#include <memory>


namespace mmo
{
	/// A list which contains all screen layers.
	static std::list<ScreenLayer> s_screenLayers;
	/// A connection with the Paint event of the event loop.
	static scoped_connection s_screenPaintCon;

	/// Paints all screen layers.
	static void PaintScreenLayers()
	{
		// Get the current graphics device
		auto& gx = GraphicsDevice::Get();

		// Iterate through all screen layers (they are already in the correct order, so
		// we don't need to do anything special here except to just iterate)
		for (const auto& layer : s_screenLayers)
		{
			// Check for disabled state
			if (layer.flags & ScreenLayerFlags::Disabled)
			{
				// Layer is marked disabled, so just skip it
				continue;
			}

			if (layer.flags & ScreenLayerFlags::IdentityTransform)
			{
				// Layer is enabled, reset transforms
				gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
				gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);
			}

			// If the layer is marked as screen space, setup orthographic matrix
			if (layer.flags & ScreenLayerFlags::IdentityProjection)
			{
				gx.SetTransformMatrix(TransformType::Projection, Matrix4::Identity);
			}

			// Call the paint function of the layer
			layer.paintFunc();
		}
	}

	void Screen::Initialize()
	{
		// Ensure that there are no existing layers
		ASSERT(s_screenLayers.empty());

		// Register paint function in the event loop
		s_screenPaintCon = EventLoop::Paint.connect(PaintScreenLayers, true);
	}

	void Screen::Destroy()
	{
		// Close the paint connection to disable screen drawing
		s_screenPaintCon.disconnect();

		// Remove all screen layers from the list.
		s_screenLayers.clear();
	}

	ScreenLayerIt Screen::AddLayer(ScreenLayerPaintFunc paintFunc, float priority, uint32 flags)
	{
		ASSERT(paintFunc);

		// Find the right place to add the screen layer
		auto it = s_screenLayers.begin();
		while (it != s_screenLayers.end() && it->priority >= priority)
		{
			++it;
		}

		// We found it, so create the object and insert it into the list
		ScreenLayer layer;
		layer.paintFunc = std::move(paintFunc);
		layer.priority = priority;
		layer.flags = flags;
		return s_screenLayers.emplace(it, std::move(layer));
	}

	void Screen::RemoveLayer(ScreenLayerIt& layer)
	{
		if (layer != s_screenLayers.end())
		{
			s_screenLayers.erase(layer);
			layer = s_screenLayers.end();
		}
	}

}
