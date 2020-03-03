// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include <string>


namespace mmo
{
	/// Enumerates relative anchor points for frames and frame elements.
	namespace anchor_point
	{
		enum Type
		{
			// No anchor point
			None = 0x00,

			// Edges
			Top,
			Right,
			Bottom,
			Left,

			// Center
			Center,

			// Corners
			TopLeft,
			TopRight,
			BottomRight,
			BottomLeft,
		};
	}

	typedef anchor_point::Type AnchorPoint;

	/// Returns the name of a given anchor point enum.
	std::string AnchorPointName(AnchorPoint point);
	/// Returns an anchor point enum value by name.
	/// @returns anchor_point::None in case of an error or invalid name.
	AnchorPoint AnchorPointByName(const std::string& name);
}
