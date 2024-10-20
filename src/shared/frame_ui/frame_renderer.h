// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/optional.h"
#include "color.h"
#include "rect.h"

#include <string>
#include <memory>


namespace mmo
{
	class Frame;
	class Color;
	class Rect;


	/// Base class for a frame renderer. Frame renderers populate geometry buffers
	/// to render frames. Each frame might have several unique states that the
	/// frame renderer knows and applies.
	class FrameRenderer
		: public NonCopyable
	{
		/// Frame may modify the m_frame property directly when the renderer is attached
		/// to a frame. This is done to prevent public access to the renderer's frame property.
		friend class Frame;

	public:
		/// Creates a new instance of the frame renderer.
		FrameRenderer(const std::string& name);
		/// Virtual default destructor.
		virtual ~FrameRenderer() = default;

	public:
		/// Gets the name of this renderer.
		inline const std::string& GetName() const { return m_name; }

		/// Gets the frame that is attached to this renderer instance.
		inline Frame* GetFrame() const { return m_frame; }

	public:
		/// Renders a given frame using this renderer instance.
		/// @param frame The frame instance to render.
		/// @param colorOverride An optional color override for tinting.
		/// @param clipper An optional clip rect.
		virtual void Render(
			optional<Color> colorOverride = optional<Color>(), 
			optional<Rect> clipper = optional<Rect>()) = 0;

		/// Called once per frame to update anything that needs to be updated every frame,
		/// like animations.
		/// @param elapsedSeconds Time since the last call to update in seconds.
		virtual void Update(float elapsedSeconds) {}

		/// Called to notify the renderer that a frame has been attached.
		virtual void NotifyFrameAttached() {}

		/// Called to notify the renderer that a frame has been detached.
		virtual void NotifyFrameDetached() {}

	protected:
		/// Name of this renderer.
		std::string m_name;
		/// The frame that is assigned to this renderer instance. The Frame class
		/// sets this directly in the SetRenderer method.
		Frame* m_frame;
	};
}
