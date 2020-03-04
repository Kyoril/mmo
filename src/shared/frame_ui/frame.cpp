// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "frame.h"
#include "frame_mgr.h"
#include "style_mgr.h"
#include "default_renderer.h"

#include "base/utilities.h"
#include "log/default_log_levels.h"

#include <algorithm>


namespace mmo
{
	Frame::Frame(const std::string & name)
		: m_name(name)
		, m_needsRedraw(true)
		, m_parent(nullptr)
		, m_visible(true)
		, m_enabled(true)
		, m_clippedByParent(false)
	{
	}

	Frame::~Frame()
	{
	}

	void Frame::SetText(const std::string & text)
	{
		// Apply new text and invalidate rendering
		m_text = text;
		m_needsRedraw = true;

		// Notify observers
		TextChanged();
	}

	bool Frame::IsVisible(bool localOnly) const
	{
		// If this is the root frame or we only care for local visibility setting...
		if (localOnly || m_parent == nullptr || IsRootFrame())
		{
			// ... return local setting
			return m_visible;
		}

		// Return parent frame visibility AND local setting
		return m_parent->IsVisible(localOnly) && m_visible;
	}

	void Frame::SetVisible(bool visible)
	{
		if (m_visible != visible)
		{
			m_visible = visible;
			VisibilityChanged();
		}
	}

	bool Frame::IsEnabled(bool localOnly) const
	{
		if (localOnly || m_parent == nullptr || IsRootFrame())
		{
			return m_enabled;
		}

		return m_parent->IsEnabled(localOnly) && m_enabled;
	}

	void Frame::SetEnabled(bool enable)
	{
		if (m_enabled != enable)
		{
			m_enabled = enable;
			EnabledStateChanged();
		}
	}

	bool Frame::IsRootFrame() const
	{
		FramePtr rootFrame = FrameManager::Get().GetTopFrame();
		if (rootFrame == nullptr)
			return false;

		return (rootFrame.get() == this);
	}

	void Frame::SetRenderer(const std::string & rendererName)
	{
		if (m_renderer != nullptr)
		{
			m_renderer->m_frame = nullptr;
			m_renderer.reset();
		}

		// TODO: This is only a temporary test until there is a renderer factory
		m_renderer = std::make_unique<DefaultRenderer>("DefaultRenderer");
		m_renderer->m_frame = this;
		m_needsRedraw = true;

		// TODO: Create new renderer instance by name

		// TODO: Register this frame for the new renderer
	}

	void Frame::SetStyle(const std::string & style)
	{
		// Create a style of the given template
		m_style = StyleManager::Get().Find(style);
		if (!m_style)
		{
			WLOG("Unable to find frame style '" << style << "' for frame '" << m_name << "'");
		}

		m_needsRedraw = true;

		// TODO: Notify renderer about this?
	}

	void Frame::SetClippedByParent(bool clipped)
	{
		if (m_clippedByParent != clipped)
		{
			m_clippedByParent = clipped;
			m_needsRedraw = true;
		}
	}

	void Frame::SetPosition(const Point & position)
	{
		m_position = position;

		// If this change somehow affects this frame, invalidate it
		if (!AnchorsSatisfyPosition())
		{
			m_needsRedraw = true;
		}
	}

	bool Frame::AnchorsSatisfyXPosition() const
	{
		return
			m_anchors.find(anchor_point::Left) != m_anchors.end() ||
			m_anchors.find(anchor_point::Right) != m_anchors.end() ||
			m_anchors.find(anchor_point::HorizontalCenter) != m_anchors.end();
	}

	bool Frame::AnchorsSatisfyYPosition() const
	{
		return
			m_anchors.find(anchor_point::Top) != m_anchors.end() ||
			m_anchors.find(anchor_point::Bottom) != m_anchors.end() ||
			m_anchors.find(anchor_point::VerticalCenter) != m_anchors.end();
	}

	bool Frame::AnchorsSatisfyWidth() const
	{
		// For this to work, there need to be an anchor set for left and right
		return 
			m_anchors.find(anchor_point::Left) != m_anchors.end() &&
			m_anchors.find(anchor_point::Right) != m_anchors.end();
	}

	bool Frame::AnchorsSatisfyHeight() const
	{
		// For this to work, there need to be an anchor set for left and right
		return
			m_anchors.find(anchor_point::Top) != m_anchors.end() &&
			m_anchors.find(anchor_point::Bottom) != m_anchors.end();
	}

	void Frame::SetAnchor(AnchorPoint point, AnchorPoint relativePoint, Frame::Pointer relativeTo)
	{
		// Create a new anchor
		m_anchors[point] = std::make_unique<Anchor>(point, relativePoint, relativeTo);
		m_needsRedraw = true;
	}

	void Frame::ClearAnchor(AnchorPoint point)
	{
		const auto it = m_anchors.find(point);
		if (it != m_anchors.end())
		{
			m_anchors.erase(it);
			m_needsRedraw = true;
		}
	}

	void Frame::Render()
	{
		// If this frame is hidden, we don't have anything to do
		if (!IsVisible())
			return;

		auto& gx = GraphicsDevice::Get();

		// If this is a top level frame, prepare render common render states so that
		// we don't have to care about them in every single child frame or child frame
		// element.
		if (m_parent == nullptr)
		{
			int32 vpW, vpH;
			gx.GetViewport(nullptr, nullptr, &vpW, &vpH);

			gx.SetTopologyType(TopologyType::TriangleList);
			gx.SetBlendMode(BlendMode::Alpha);
			gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
			gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);
			gx.SetTransformMatrix(TransformType::Projection, Matrix4::MakeOrthographic(0.0f, static_cast<float>(vpW), static_cast<float>(vpH), 0.0f, 0.0f, 100.0f));
		}

		// Draw self
		DrawSelf();

		// Whether a clip rect has been set by this function call to avoid calling ResetClipRect on
		// the graphics library more often than needed
		bool hasClipRectSet = false;

		// Draw children
		for (const auto& child : m_children)
		{
			if (child->IsClippedByParent())
			{
				// TODO: set clip rect
				hasClipRectSet = true;
			}
			else if (hasClipRectSet)
			{
				// Reset clip rect
				gx.ResetClipRect();
				hasClipRectSet = false;
			}

			// Render child frame
			child->Render();
		}
	}

	void Frame::Update(float elapsed)
	{
		// Try to call the renderer's update method if we have a valid renderer
		if (m_renderer != nullptr)
		{
			m_renderer->Update(elapsed);
		}
	}

	void Frame::AddChild(Frame::Pointer frame)
	{
		m_children.push_back(frame);
	}

	Rect Frame::GetRelativeFrameRect()
	{
		Size mySize = GetPixelSize();
		if (AnchorsSatisfyWidth())
		{
			// TODO: Determine width by using anchors
		}
		if (AnchorsSatisfyHeight())
		{
			// TODO: Determine height by using anchors
		}

		// Return the rectangle with the calculated size
		return Rect(Point(), mySize);
	}

	static void AdjustRectToAnchor(Rect& r, const Rect& parentRect, AnchorPoint p, const Point& offset)
	{
		switch (p)
		{
		case AnchorPoint::Top:
			r.Offset(Point(0.0f, offset.y));
			break;
		case AnchorPoint::Left:
			r.Offset(Point(offset.x, 0.0f));
			break;
		case AnchorPoint::Right:
			r.Offset(Point(
				parentRect.GetWidth() - r.GetWidth() - offset.x,
				0.0f));
			break;
		case AnchorPoint::Bottom:
			r.Offset(Point(
				0.0f,
				parentRect.bottom - r.GetHeight() - offset.y));
			break;

		case AnchorPoint::HorizontalCenter:
			r.Offset(Point(
				parentRect.GetWidth() * 0.5f - r.GetWidth() * 0.5f + offset.x,
				0.0f
			));
			break;

		case AnchorPoint::VerticalCenter:
			r.Offset(Point(
				0.0f,
				parentRect.GetHeight() * 0.5f - r.GetHeight() * 0.5f + offset.y
			));
			break;
		}
	}

	Rect Frame::GetAbsoluteFrameRect()
	{
		// First, obtain the relative frame rect
		Rect r = GetRelativeFrameRect();

		// This rect will contain the absolute parent rectangle
		Rect parentRect;

		if (m_parent == nullptr)
		{
			// Obtain the current viewport size in pixels in case this 
			int32 vpW, vpH;
			GraphicsDevice::Get().GetViewport(nullptr, nullptr, &vpW, &vpH);

			// No parent frame available, then the screen rect is the parent rect
			parentRect.SetSize(Size(vpW, vpH));
		}
		else
		{
			// Use the absolute frame rect of the parent
			parentRect = m_parent->GetAbsoluteFrameRect();
		}

		// Add parent rect offset to the relative rect
		r.Offset(parentRect.GetPosition());

		// Apply custom position
		Point localPosition = m_position;
		if (AnchorsSatisfyXPosition())
		{
			// TODO: Apply anchor position

		}
		if (AnchorsSatisfyYPosition())
		{
			// TODO: Apply anchor position
		}

		// Move rectangle
		r.Offset(localPosition);

		// Return the current rect
		return r;
	}
	
	void Frame::DrawSelf()
	{
		BufferGeometry();
		QueueGeometry();
	}

	void Frame::BufferGeometry()
	{
		if (m_needsRedraw)
		{
			// dispose of already cached geometry.
			m_geometryBuffer.Reset();

			// Signal rendering started
			RenderingStarted();

			// If there is a renderer created, use it to render this frame, otherwise call the frame's internal
			// PopulateGeometryBuffer virtual method.
			if (m_renderer != nullptr)
			{
				m_renderer->Render();
			}
			else
			{
				PopulateGeometryBuffer();
			}

			// Signal rendering ended
			RenderingEnded();

			// mark ourselves as no longer needed a redraw.
			m_needsRedraw = false;
		}
	}

	void Frame::QueueGeometry()
	{
		// Draw the geometry buffer
		m_geometryBuffer.Draw();
	}

	GeometryBuffer & Frame::GetGeometryBuffer()
	{ 
		return m_geometryBuffer;
	}
}
