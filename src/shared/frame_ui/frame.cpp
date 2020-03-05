// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "frame.h"
#include "frame_mgr.h"
#include "style_mgr.h"
#include "default_renderer.h"
#include "button_renderer.h"

#include "base/utilities.h"
#include "base/macros.h"
#include "log/default_log_levels.h"

#include <algorithm>


namespace mmo
{
	Frame::Frame(const std::string& type, const std::string & name)
		: m_type(type)
		, m_name(name)
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

	void Frame::Copy(Frame & other)
	{
		// Copy style
		other.m_style = m_style;

		// Apply renderer property
		other.SetRenderer(m_renderer->GetName());

		// Apply other properties
		other.m_enabled = m_enabled;
		other.m_visible = m_visible;
		other.m_clippedByParent = m_clippedByParent;
		other.m_pixelSize = m_pixelSize;
		other.m_position = m_position;
		other.m_text = m_text;

		// Copy all children and their children
		for (const auto& child : m_children)
		{
			// Create a copy of the child frame
			FramePtr copiedChild = FrameManager::Get().Create(m_type, "", true);
			ASSERT(copiedChild);

			// Copy properties over to child frame
			child->Copy(*copiedChild);

			// Add child copy to the copied frame
			other.m_children.emplace_back(std::move(copiedChild));
		}
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
		if (rendererName == "ButtonRenderer")
		{
			m_renderer = std::make_unique<ButtonRenderer>("ButtonRenderer");
		}
		else
		{
			m_renderer = std::make_unique<DefaultRenderer>("DefaultRenderer");
		}

		if (m_renderer)
		{
			m_renderer->m_frame = this;
			m_needsRedraw = true;
		}

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

	bool Frame::IsHovered() const
	{
		return this == FrameManager::Get().GetHoveredFrame().get();
	}

	void Frame::Invalidate()
	{
		m_needsRedraw = true;
	}

	Frame::Pointer Frame::GetChildFrameAt(const Point & position, bool allowDisabled)
	{
		// Iterate through all children
		for (auto& child : m_children)
		{
			// Check the rectangle
			const Rect childRect = child->GetAbsoluteFrameRect();
			if (childRect.IsPointInRect(position) &&
				(allowDisabled || child->IsEnabled()))
			{
				return child->GetChildFrameAt(position, allowDisabled);
			}
		}

		return shared_from_this();
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
		// Use the internal size property as the default value
		Size mySize = GetPixelSize();

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
		const Rect parentRect = GetParentRect();

		// Add parent rect offset to the relative rect
		r.Offset(parentRect.GetPosition());

		// Apply custom position
		Point localPosition = m_position;
		if (AnchorsSatisfyXPosition())
		{
			// TODO
		}
		if (AnchorsSatisfyYPosition())
		{
			// TODO
		}
		if (AnchorsSatisfyWidth())
		{
			const auto leftAnchorIt = m_anchors.find(anchor_point::Left);
			ASSERT(leftAnchorIt != m_anchors.end());
			const auto rightAnchorIt = m_anchors.find(anchor_point::Right);
			ASSERT(rightAnchorIt != m_anchors.end());

			// TODO: Apply anchor offset values
			r.SetWidth(parentRect.GetWidth());
		}
		if (AnchorsSatisfyHeight())
		{
			const auto topAnchorIt = m_anchors.find(anchor_point::Top);
			ASSERT(topAnchorIt != m_anchors.end());
			const auto bottomAnchorIt = m_anchors.find(anchor_point::Bottom);
			ASSERT(bottomAnchorIt != m_anchors.end());

			// TODO: Apply anchor offset values
			r.SetHeight(parentRect.GetHeight());
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

	Rect Frame::GetParentRect()
	{
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

		return parentRect;
	}

	GeometryBuffer & Frame::GetGeometryBuffer()
	{ 
		return m_geometryBuffer;
	}
}
