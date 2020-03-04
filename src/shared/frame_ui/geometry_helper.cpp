// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "geometry_helper.h"
#include "geometry_buffer.h"


namespace mmo
{
	void GeometryHelper::CreateRect(GeometryBuffer & buffer, Point position, Rect src, uint16 texW, uint16 texH)
	{
		CreateRect(buffer, Rect(position, src.GetSize()), src, texW, texH);
	}

	void GeometryHelper::CreateRect(GeometryBuffer & buffer, Rect dst, Rect src, uint16 texW, uint16 texH)
	{
		// Calculate uv rectangle
		const Rect uvRect{
			src.GetPosition() / Point(texW, texH),
			Size(src.GetWidth() / static_cast<float>(texW), src.GetHeight() / static_cast<float>(texH))
		};

		// Generate vertex data
		const GeometryBuffer::Vertex vertices[6]{
			// First triangle
			{ { dst.left,	dst.bottom,		0.0f }, 0xffffffff, { uvRect.left,	uvRect.top		} },
			{ { dst.left,	dst.top,		0.0f }, 0xffffffff, { uvRect.left,	uvRect.bottom	} },
			{ { dst.right,	dst.top,		0.0f }, 0xffffffff, { uvRect.right,	uvRect.bottom	} },
			// Second triangle
			{ { dst.right,	dst.top,		0.0f }, 0xffffffff, { uvRect.right,	uvRect.bottom	} },
			{ { dst.right,	dst.bottom,		0.0f }, 0xffffffff, { uvRect.right,	uvRect.top		} },
			{ { dst.left,	dst.bottom,		0.0f }, 0xffffffff, { uvRect.left,	uvRect.top		} }
		};

		// Append geometry to the buffer
		buffer.AppendGeometry(vertices, 6);
	}
}
