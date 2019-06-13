// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "frame.h"


namespace mmo
{
	Frame::Frame(const std::string & name)
		: m_name(name)
		, m_needsRedraw(true)
	{
		// Create a new geometry buffer for this frame
		m_geometryBuffer = std::make_unique<GeometryBuffer>();
	}

	Frame::~Frame()
	{
	}

	void Frame::Render()
	{
		DrawSelf();

		// TODO: Draw children
	}

	void Frame::Update(float elapsed)
	{
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

	}
}
