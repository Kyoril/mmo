
#include "frame_object.h"
#include "frame.h"


namespace mmo
{
	FrameObject::FrameObject(Frame & frame)
		: m_frame(frame)
	{
	}

	Rect FrameObject::GetArea() const
	{
		// Obtain the parent frame area
		Rect parentFrameArea = Rect(Point(), m_frame.GetPixelSize());

		// Check anchor points


		return Rect();
	}

	bool FrameObject::GetAnchorPointOffset(AnchorPoint point, Point & offset)
	{
		auto it = m_anchorPointOffsets.find(point);
		if (it == m_anchorPointOffsets.end())
			return false;

		offset = it->second;
		return true;
	}
}
