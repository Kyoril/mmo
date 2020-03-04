// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "frame_component.h"


namespace mmo
{
	FrameComponent::FrameComponent()
	{
	}
	
	Rect FrameComponent::GetArea() const
	{
		// Obtain the parent frame area
		//Rect parentFrameArea = Rect(Point(), m_frame.GetPixelSize());

		// Check anchor points


		return Rect();
	}

	bool FrameComponent::GetAnchorPointOffset(AnchorPoint point, Point & offset)
	{
		auto it = m_anchorPointOffsets.find(point);
		if (it == m_anchorPointOffsets.end())
			return false;

		offset = it->second;
		return true;
	}
}
