// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "frame_component.h"
#include "frame.h"
#include "frame_mgr.h"

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

		const auto scale = FrameManager::Get().GetUIScale();

		// Apply inset
		r.left += m_areaInset.left * scale.x;
		r.top += m_areaInset.top * scale.y;
		r.bottom -= m_areaInset.bottom * scale.x;
		r.right -= m_areaInset.right * scale.y;

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
