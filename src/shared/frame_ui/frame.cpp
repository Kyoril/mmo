// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "frame.h"

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

	void Frame::Render()
	{
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

	void Frame::SetAnchor(AnchorPoint point, Point offset)
	{
		m_anchorPoint = point;
		m_anchorOffset = offset;
		m_needsRedraw = true;
	}

	void Frame::AddChild(Frame::Pointer frame)
	{
		m_children.push_back(frame);
	}

	FrameLayer & Frame::AddLayer()
	{
		auto layer = std::make_unique<FrameLayer>();

		// TODO: Configure layer somehow?

		return *m_layers.emplace_back(std::move(layer));
	}

	void Frame::RemoveLayer(FrameLayer & layer)
	{
		// Warning: This method isn't very performant. Although removing from a vector can be
		// improved by swapping with back and popping the last element from the vector, this
		// would mean that the order in which layers are drawn would be affected - which we
		// don't want to. So call this method not too often (if you every need to call it at
		// all that is).

		// Find that layer
		auto layerIt = std::find_if(m_layers.begin(), m_layers.end(), [&layer](std::unique_ptr<FrameLayer>& b) -> bool {
			return &layer == b.get();
		});

		// Remove that layer from the list of layers
		if (layerIt != m_layers.end())
		{
			m_layers.erase(layerIt);
		}
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
		AdjustRectToAnchor(r, parentRect, m_anchorPoint, m_anchorOffset);

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
		for (const auto& layer : m_layers)
		{
			layer->Render(*m_geometryBuffer);
		}
	}

}
