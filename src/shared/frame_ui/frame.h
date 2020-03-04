// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "geometry_buffer.h"
#include "rect.h"
#include "anchor_point.h"
#include "style.h"
#include "frame_renderer.h"

#include "base/typedefs.h"
#include "base/signal.h"

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
		typedef std::shared_ptr<Frame> Pointer;

	public:
		/// Fired when rendering of the frame began.
		signal<void()> RenderingStarted;
		/// Fired when rendering of the frame ended.
		signal<void()> RenderingEnded;
		/// Fired when the text of this frame was changed.
		signal<void()> TextChanged;
		/// Fired when the enabled state of this frame was changed.
		signal<void()> EnabledStateChanged;
		/// Fired when the frame's visibility changed.
		signal<void()> VisibilityChanged;

	public:
		Frame(const std::string& name);
		virtual ~Frame();

	public:
		/// Gets the text of this frame.
		inline const std::string& GetText() const { return m_text; }
		/// Sets the text of this frame.
		void SetText(const std::string& text);
		/// Determines whether the frame is currently visible.
		/// @param localOnly If set to true, the parent frame's visibility setting is ignored.
		/// @returns true, if this frame is currently visible.
		bool IsVisible(bool localOnly = true) const;
		/// Sets the visibility of this frame.
		/// @param visible Whether the frame will be visible or not.
		void SetVisible(bool visible);
		/// Syntactic sugar for SetVisible(true).
		inline void Show() { SetVisible(true); }
		/// Syntactic sugar for SetVisible(false).
		inline void Hide() { SetVisible(false); }
		/// Determines whether the frame is currently enabled.
		/// @param localOnly If set to true, the parent frame's enabled setting is ignored.
		/// @returns true, if this frame is currently enabled.
		bool IsEnabled(bool localOnly = true) const;
		/// Enables or disables this frame.
		/// @param enable Whether the frame should be enabled or disabled.
		void SetEnabled(bool enable);
		/// Syntactic sugar for SetEnabled(true).
		inline void Enable() { SetEnabled(true); }
		/// Syntactic sugar for SetEnabled(false).
		inline void Disable() { SetEnabled(false); }
		/// Determines if this window is the root frame.
		bool IsRootFrame() const;
		/// Sets the renderer by name.
		void SetRenderer(const std::string& rendererName);
		/// Sets the window style by name.
		void SetStyle(const std::string& style);
		/// Gets the style instance if there is any.
		inline const StylePtr GetStyle() const { return m_style; }
		/// Gets the renderer instance if there is any.
		inline const FrameRenderer* GetRenderer() const { return m_renderer.get(); }


	public:
		/// Gets a string object holding the name of this frame.
		inline const std::string& GetName() const { return m_name; }

		void Render();

		virtual void Update(float elapsed);

		/// Gets the pixel size of this frame.
		virtual inline Size GetPixelSize() const { return m_pixelSize; }
		/// Sets the pixel size of this frame.
		virtual inline void SetPixelSize(Size newSize) { m_pixelSize = newSize; m_needsRedraw = true; }

		virtual void SetOrigin(AnchorPoint point, Point offset = Point());
		/// Sets anchor points of this frame in the parent frame, which tells where to position the frame.
		virtual void SetAnchorPoints(uint8 points);

		virtual void AddChild(Pointer frame);
		
	protected:

		virtual Rect GetRelativeFrameRect();
		/// Used to get the frame rectangle.
		virtual Rect GetAbsoluteFrameRect();
		/// Gets the frame rectangle in screen space coordinates.
		virtual Rect GetScreenFrameRect();

		virtual void UpdateSelf(float elapsed);

		virtual void DrawSelf();

		void BufferGeometry();

		void QueueGeometry();
		/// Allows for custom geometry buffer population for custom frame classes.
		virtual void PopulateGeometryBuffer() {}

	protected:

		typedef std::vector<Pointer> ChildList;

		std::string m_name;
		bool m_needsRedraw;
		std::string m_text;
		bool m_visible;
		bool m_enabled;
		ChildList m_children;
		std::unique_ptr<GeometryBuffer> m_geometryBuffer;
		/// The current size of this frame in pixels.
		Size m_pixelSize;
		/// The anchor point used for positioning.
		AnchorPoint m_originPoint;

		Point m_anchorOffset;

		Frame* m_parent;
		/// Window style instance.
		StylePtr m_style;
		/// Window renderer instance.
		std::unique_ptr<FrameRenderer> m_renderer;
	};

	typedef Frame::Pointer FramePtr;

}
