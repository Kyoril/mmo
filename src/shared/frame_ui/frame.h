// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/signal.h"

#include "geometry_buffer.h"
#include "rect.h"

#include <memory>
#include <string>
#include <vector>
#include <map>


namespace mmo
{
	/// Enumerated type used when specifying vertical alignments.
	enum class VerticalAlignment
	{
		/// Frame's position specifies an offset of it's top edge from the top edge of it's parent.
		Top,
		/// Frame's position specifies an offset of it's vertical center from the vertical center of it's parent.
		Center,
		/// Frame's position specifies an offset of it's bottom edge from the bottom edge of it's parent.
		Bottom
	};


	/// Enumerated type used when specifying horizontal alignments.
	enum class HorizontalAlignment
	{
		/// Frame's position specifies an offset of it's left edge from the left edge of it's parent.
		Left,
		/// Frame's position specifies an offset of it's horizontal center from the horizontal center of it's parent.
		Center,
		/// Frame's position specifies an offset of it's right edge from the right edge of it's parent.
		Right
	};


	/// Enumerated type used for specifying Frame::update mode to be used. Note that
	/// the setting specified will also have an effect on child window content; for
	/// Never and Visible, if the parent's update function is not called, then no
	/// child frame will have it's update function called either - even if it
	/// specifies Always as it's FrameUpdateMode.
	enum class FrameUpdateMode
	{
		/// Always call the Frame::Update function for this frame.
		Always,
		/// Never call the Frame::Update function for this frame.
		Never,
		/// Only call the Frame::Update function for this frame if it is visible.
		Visible,
	};


	/// This is the base class to represent a simple frame.
	class Frame
	{
	public:

		signal<void()> RenderingStarted;
		signal<void()> RenderingEnded;

	public:
		Frame(const std::string& name);
		virtual ~Frame();

	public:
		/// Gets a string object holding the name of this frame.
		inline const std::string& GetName() const { return m_name; }

		void Render();

		virtual void Update(float elapsed);

		virtual inline Size GetPixelSize() const { return m_pixelSize; }

	protected:

		virtual void UpdateSelf(float elapsed);

		virtual void DrawSelf();

		void BufferGeometry();

		void QueueGeometry();

		virtual void PopulateGeometryBuffer();

	protected:

		typedef std::vector<Frame*> ChildList;

		std::string m_name;
		bool m_needsRedraw;
		ChildList m_children;
		std::unique_ptr<GeometryBuffer> m_geometryBuffer;
		/// The current size of this frame in pixels.
		Size m_pixelSize;
	};

	typedef std::shared_ptr<Frame> FramePtr;
}
