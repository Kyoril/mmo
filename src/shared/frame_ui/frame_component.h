// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "rect.h"
#include "color.h"

#include "base/non_copyable.h"

#include <map>


namespace mmo
{
	class Frame;


	/// This is the base class for a frame object which is renderable
	/// and has some placement logic.
	class FrameComponent 
		: public NonCopyable
		, public std::enable_shared_from_this<FrameComponent>
	{
	public:
		FrameComponent(Frame& frame);
		virtual ~FrameComponent() = default;

	public:
		virtual std::unique_ptr<FrameComponent> Copy() const = 0;

	protected:
		void CopyBaseAttributes(FrameComponent& other) const;

	public:
		/// Gets the size of this frame object in pixels.
		virtual Size GetSize() const { return Size(); }
		/// Gets the area rectangle of this object.
		virtual Rect GetArea(const Rect& area) const;

		inline const Rect& GetInset() const { return m_areaInset; }
		/// Sets the area inset.
		inline void SetInset(const Rect& rect) { m_areaInset = rect; }
		/// Sets the frame that this component belongs to.
		inline void SetFrame(Frame& frame) { m_frame = &frame; }

	public:
		/// Renders the frame object.
		virtual void Render(const Rect& area, const Color& color = Color::White) = 0;

	protected:
		/// The frame that owns this component.
		Frame* m_frame;
		/// The area inset.
		Rect m_areaInset;
	};
}
