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
		case anchor_point::HorizontalCenter:
			return "H_CENTER";
		case anchor_point::VerticalCenter:
			return "V_CENTER";
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
		else if (_stricmp(name.c_str(), "H_CENTER") == 0)
		{
			return anchor_point::HorizontalCenter;
		}
		else if (_stricmp(name.c_str(), "V_CENTER") == 0)
		{
			return anchor_point::VerticalCenter;
		}

		return AnchorPoint::None;
	}

	void Anchor::ApplyToAbsRect(Rect & rect, const Rect & parentRect, bool hasOppositeAnchor)
	{
		const float offset = GetValueByPoint(parentRect, m_relativePoint);
		switch (m_point)
		{
		case anchor_point::Left:
			if (!hasOppositeAnchor)
				rect.right = offset + rect.GetWidth();
			rect.left = offset;
			break;
		case anchor_point::Top:
			if (!hasOppositeAnchor)
				rect.bottom = offset + rect.GetHeight();
			rect.top = offset;
			break;
		case anchor_point::Right:
			if (!hasOppositeAnchor)
				rect.left = offset - rect.GetWidth();
			rect.right = offset;
			break;
		case anchor_point::Bottom:
			if (!hasOppositeAnchor)
				rect.top = offset - rect.GetHeight();
			rect.bottom = offset;
			break;
		case anchor_point::HorizontalCenter:
			{
				const float w = rect.GetWidth() * 0.5f;
				rect.left = offset - w;
				rect.right = offset + w;
			}
			break;
		case anchor_point::VerticalCenter:
			{
				const float h = rect.GetHeight() * 0.5f;
				rect.top = offset - h;
				rect.bottom = offset + h;
			}
			break;
		default:
			// TODO
			break;
		}
	}

	float Anchor::GetValueByPoint(const Rect & absRect, AnchorPoint point)
	{
		switch (point)
		{
		case anchor_point::Left:
			return absRect.left;
		case anchor_point::Top:
			return absRect.top;
		case anchor_point::Right:
			return absRect.right;
		case anchor_point::Bottom:
			return absRect.bottom;
		case anchor_point::HorizontalCenter:
			return absRect.left + absRect.GetWidth() * 0.5f;
		case anchor_point::VerticalCenter:
			return absRect.top + absRect.GetHeight() * 0.5f;
		}

		return 0.0f;
	}
}
