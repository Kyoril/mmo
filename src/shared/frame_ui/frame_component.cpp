// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "frame_component.h"
#include "frame.h"

#include "base/macros.h"


namespace mmo
{
	FrameComponent::FrameComponent(Frame& frame)
		: m_frame(&frame)
	{
	}
	
	Rect FrameComponent::GetArea() const
	{
		ASSERT(m_frame);

		// Check anchor points
		Rect r = m_frame->GetAbsoluteFrameRect();

		// Apply inset
		r.left += m_areaInset.left;
		r.top += m_areaInset.top;
		r.bottom -= m_areaInset.bottom;
		r.right -= m_areaInset.right;

		return r;
	}
}
