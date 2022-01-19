// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "geometry_helper.h"
#include "geometry_buffer.h"


namespace mmo
{
	void GeometryHelper::CreateRect(GeometryBuffer & buffer, argb_t color, Point position, Rect src, uint16 texW, uint16 texH)
	{
		CreateRect(buffer, color, Rect(position, src.GetSize()), src, texW, texH);
	}

	void GeometryHelper::CreateRect(GeometryBuffer & buffer, argb_t color, Rect dst, Rect src, uint16 texW, uint16 texH)
	{
		// Calculate uv rectangle
		const Rect uvRect{
			src.GetPosition() / Point(texW, texH),
			Size(src.GetWidth() / static_cast<float>(texW), src.GetHeight() / static_cast<float>(texH))
		};

		// Generate vertex data
		const GeometryBuffer::Vertex vertices[6]{
			// First triangle
			{ { dst.left,	dst.bottom,		0.0f },	color, { uvRect.left,	uvRect.bottom		} },
			{ { dst.left,	dst.top,			0.0f },	color, { uvRect.left,	uvRect.top	} },
			{ { dst.right,	dst.top,			0.0f }, color, { uvRect.right,	uvRect.top } },
			// Second triangle
			{ { dst.right,	dst.top,			0.0f }, color, { uvRect.right,	uvRect.top	} },
			{ { dst.right,	dst.bottom,		0.0f }, color, { uvRect.right,	uvRect.bottom		} },
			{ { dst.left,	dst.bottom,		0.0f }, color, { uvRect.left,	uvRect.bottom		} }
		};

		// Append geometry to the buffer
		buffer.AppendGeometry(vertices, 6);
	}

	void GeometryHelper::CreateRect(GeometryBuffer & buffer, argb_t color, Rect dst)
	{
		// Generate vertex data
		const GeometryBuffer::Vertex vertices[6]{
			// First triangle
			{ { dst.left,	dst.bottom,		0.0f }, color, { 0.0f, 0.0f } },
			{ { dst.left,	dst.top,		0.0f }, color, { 0.0f, 1.0f } },
			{ { dst.right,	dst.top,		0.0f }, color, { 1.0f, 1.0f } },
			// Second triangle
			{ { dst.right,	dst.top,		0.0f }, color, { 1.0f, 1.0f } },
			{ { dst.right,	dst.bottom,		0.0f }, color, { 1.0f, 0.0f } },
			{ { dst.left,	dst.bottom,		0.0f }, color, { 0.0f, 0.0f } }
		};

		// Append geometry to the buffer
		buffer.AppendGeometry(vertices, 6);
	}
}
