// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "frame.h"
#include "frame_mgr.h"

#include "base/utilities.h"

#include <algorithm>


namespace mmo
{
	Frame::Frame(const std::string & name)
		: m_name(name)
		, m_needsRedraw(true)
		, m_parent(nullptr)
	{
		// Create a new geometry buffer for this frame
		m_geometryBuffer = std::make_unique<GeometryBuffer>();
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

	void Frame::Render()
	{
		// If this frame is hidden, we don't have anything to do
		if (!IsVisible())
			return;

		// If this is a top level frame, prepare render common render states so that
		// we don't have to care about them in every single child frame or child frame
		// element.
		if (m_parent == nullptr)
		{
			auto& gx = GraphicsDevice::Get();

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

		// Draw children
		for (const auto& child : m_children)
		{
			child->Render();
		}
	}

	void Frame::Update(float elapsed)
	{

	}

	void Frame::SetOrigin(AnchorPoint point, Point offset)
	{
		m_originPoint = point;
		m_anchorOffset = offset;
		m_needsRedraw = true;
	}

	void Frame::SetAnchorPoints(uint8 points)
	{
	}

	void Frame::AddChild(Frame::Pointer frame)
	{
		m_children.push_back(frame);
	}

	Rect Frame::GetRelativeFrameRect()
	{
		Rect r;

		// Set the size of this rectangle
		r.SetSize(GetPixelSize());

		return r;
	}

	static void AdjustRectToAnchor(Rect& r, const Rect& parentRect, AnchorPoint p, const Point& offset)
	{
		switch (p)
		{
		case AnchorPoint::TopLeft:
		case AnchorPoint::Top:
		case AnchorPoint::Left:
			r.Offset(offset);
			break;
		case AnchorPoint::TopRight:
		case AnchorPoint::Right:
			r.Offset(Point(
				parentRect.GetWidth() - r.GetWidth() - offset.x,
				offset.y));
			break;
		case AnchorPoint::BottomRight:
			r.Offset(Point(
				parentRect.GetWidth() - r.GetWidth() - offset.x,
				parentRect.GetHeight() - r.GetHeight() - offset.y));
			break;
		case AnchorPoint::BottomLeft:
		case AnchorPoint::Bottom:
			r.Offset(Point(
				offset.x,
				parentRect.m_bottom - r.GetHeight() - offset.y));
			break;

		case AnchorPoint::Center:
			r.Offset(Point(
				parentRect.GetWidth() * 0.5f - r.GetWidth() * 0.5f + offset.x,
				parentRect.GetHeight() * 0.5f - r.GetHeight() * 0.5f + offset.y
			));
			break;
		}
	}

	Rect Frame::GetAbsoluteFrameRect()
	{
		Rect r = GetRelativeFrameRect();

		int32 vpW, vpH;
		GraphicsDevice::Get().GetViewport(nullptr, nullptr, &vpW, &vpH);

		// Offset by parent position
		Rect parentRect = Rect(0.0f, 0.0f, static_cast<float>(vpW), static_cast<float>(vpH));	// TODO: Get screen size
		if (m_parent != nullptr)
		{
			parentRect = m_parent->GetAbsoluteFrameRect();
		}

		// Add parent rect offset
		r.Offset(parentRect.GetPosition());

		// Adjust rectangle based on anchor position
		AdjustRectToAnchor(r, parentRect, m_originPoint, m_anchorOffset);

		return r;
	}

	Rect Frame::GetScreenFrameRect()
	{
		Rect frameRect = GetAbsoluteFrameRect();

		// TODO: Derive screen size


		return frameRect;
	}

	void Frame::UpdateSelf(float elapsed)
	{

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
			m_geometryBuffer->Reset();

			// Signal rendering started
			RenderingStarted();

			// Get derived class or WindowRenderer to re-populate geometry buffer.
			PopulateGeometryBuffer();

			// Signal rendering ended
			RenderingEnded();

			// mark ourselves as no longer needed a redraw.
			m_needsRedraw = false;
		}
	}

	void Frame::QueueGeometry()
	{
		// Draw the geometry buffer
		m_geometryBuffer->Draw();
	}

	void Frame::PopulateGeometryBuffer()
	{
		// TODO: Call the window renderer's draw method
	}
}
