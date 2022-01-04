// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "frame_component.h"
#include "frame.h"

#include "base/macros.h"


namespace mmo
{
	FrameComponent::FrameComponent(Frame& frame)
		: m_frame(&frame)
	{
	}
	
	void FrameComponent::CopyBaseAttributes(FrameComponent & other) const
	{
		other.SetInset(m_areaInset);
	}

	Rect FrameComponent::GetArea(const Rect& area) const
	{
		ASSERT(m_frame);

		// Check anchor points
		Rect r = area;

		// Apply inset
		r.left += m_areaInset.left;
		r.top += m_areaInset.top;
		r.bottom -= m_areaInset.bottom;
		r.right -= m_areaInset.right;

		return r;
	}

	void FrameComponent::SetFrame(Frame& frame)
	{
		if (m_frame != &frame)
		{
			m_frame = &frame;
			
			OnFrameChanged();	
		}
	}
}
