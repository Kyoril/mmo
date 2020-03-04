// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "geometry_helper.h"
#include "geometry_buffer.h"


namespace mmo
{
	void GeometryHelper::CreateRect(GeometryBuffer & buffer, Point position, Rect src, uint16 texW, uint16 texH)
	{
		// Calculate uv rectangle
		const Rect uvRect{
			src.GetPosition() / Point(texW, texH),
			Size(src.GetWidth() / static_cast<float>(texW), src.GetHeight() / static_cast<float>(texH))
		};

		// Calculate screen rect
		const Rect scrn{
			position,
			src.GetSize()
		};

		// Calculate size
		const float x = position.x;
		const float y = position.y;
		const float w = src.GetWidth();
		const float h = src.GetHeight();

		// Generate vertex data
		const GeometryBuffer::Vertex vertices[6] {
			// First triangle
			{ { scrn.left,	scrn.bottom,	0.0f }, 0xffffffff, { uvRect.left,	uvRect.top		} },
			{ { scrn.left,	scrn.top,		0.0f }, 0xffffffff, { uvRect.left,	uvRect.bottom	} },
			{ { scrn.right,	scrn.top,		0.0f }, 0xffffffff, { uvRect.right,	uvRect.bottom	} },
			// Second triangle
			{ { scrn.right,	scrn.top,		0.0f }, 0xffffffff, { uvRect.right,	uvRect.bottom	} },
			{ { scrn.right,	scrn.bottom,	0.0f }, 0xffffffff, { uvRect.right,	uvRect.top		} },
			{ { scrn.left,	scrn.bottom,	0.0f }, 0xffffffff, { uvRect.left,	uvRect.top		} }
		};

		// Append geometry to the buffer
		buffer.AppendGeometry(vertices, 6);
	}
}
