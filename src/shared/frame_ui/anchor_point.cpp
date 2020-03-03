// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "anchor_point.h"

#include "base/utilities.h"


namespace mmo
{

	std::string AnchorPointName(AnchorPoint point)
	{
		switch (point)
		{
		case anchor_point::Top:
			return "TOP";
		case anchor_point::Right:
			return "RIGHT";
		case anchor_point::Bottom:
			return "BOTTOM";
		case anchor_point::Left:
			return "LEFT";
		case anchor_point::Center:
			return "CENTER";
		case anchor_point::TopLeft:
			return "TOPLEFT";
		case anchor_point::TopRight:
			return "TOPRIGHT";
		case anchor_point::BottomRight:
			return "BOTTOMRIGHT";
		case anchor_point::BottomLeft:
			return "BOTTOMLEFT";
		default:
			return "NONE";
		}
	}

	AnchorPoint AnchorPointByName(const std::string & name)
	{
		if (_stricmp(name.c_str(), "TOP") == 0)
		{
			return anchor_point::Top;
		}
		else if (_stricmp(name.c_str(), "RIGHT") == 0)
		{
			return anchor_point::Right;
		}
		else if (_stricmp(name.c_str(), "BOTTOM") == 0)
		{
			return anchor_point::Bottom;
		}
		else if (_stricmp(name.c_str(), "LEFT") == 0)
		{
			return anchor_point::Left;
		}
		else if (_stricmp(name.c_str(), "CENTER") == 0)
		{
			return anchor_point::Center;
		}
		else if (_stricmp(name.c_str(), "TOPLEFT") == 0)
		{
			return anchor_point::TopLeft;
		}
		else if (_stricmp(name.c_str(), "TOPRIGHT") == 0)
		{
			return anchor_point::TopRight;
		}
		else if (_stricmp(name.c_str(), "BOTTOMRIGHT") == 0)
		{
			return anchor_point::BottomRight;
		}
		else if (_stricmp(name.c_str(), "BOTTOMLEFT") == 0)
		{
			return anchor_point::BottomLeft;
		}

		return AnchorPoint::None;
	}
}
