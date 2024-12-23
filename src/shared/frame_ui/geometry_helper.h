// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "rect.h"
#include "color.h"

#include "base/typedefs.h"
#include "base/non_copyable.h"


namespace mmo
{
	class GeometryBuffer;


	/// 
	class GeometryHelper final
		: public NonCopyable
	{
	private:
		// Prevent construction as this is a static class
		GeometryHelper() = default;

	public:
		/// Adds a rectangle to the geometry buffer.
		/// @param buffer The geometry buffer to add to.
		/// @param position The target position on screen.
		/// @param src The source area on the texture in texels.
		/// @param texW Width of the texture (used to calculate uv coordinates).
		/// @param texH Height of the texture (used to calculate uv coordinates).
		static void CreateRect(GeometryBuffer& buffer, argb_t color, Point position, Rect src, uint16 texW, uint16 texH);
		static void CreateRect(GeometryBuffer& buffer, argb_t color, Rect dst, Rect src, uint16 texW, uint16 texH);
		static void CreateRect(GeometryBuffer& buffer, argb_t color, Rect dst);
	};
}
