// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "rect.h"
#include "anchor_point.h"

#include "base/non_copyable.h"

#include <map>


namespace mmo
{
	class GeometryBuffer;
	class Frame;


	/// This is the base class for a frame object which is renderable
	/// and has some placement logic.
	class FrameComponent 
		: public NonCopyable
	{
	public:
		FrameComponent(Frame& frame);
		virtual ~FrameComponent() = default;

	public:
		/// Gets the size of this frame object in pixels.
		virtual Size GetSize() const { return Size(); }
		/// Gets the area rectangle of this object.
		virtual Rect GetArea() const;

	public:
		/// Renders the frame object.
		virtual void Render(GeometryBuffer& buffer) const = 0;

	protected:
		bool GetAnchorPointOffset(AnchorPoint point, Point& offset);

	protected:
		Frame& m_frame;
		/// Map of anchor points.
		std::map<AnchorPoint, Point> m_anchorPointOffsets;
	};
}
