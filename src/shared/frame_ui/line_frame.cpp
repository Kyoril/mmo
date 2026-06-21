// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "line_frame.h"

#include "frame_mgr.h"
#include "geometry_buffer.h"
#include "graphics/texture_mgr.h"

#include <cmath>
#include <cstdlib>

namespace mmo
{
	namespace
	{
		float ReadFloat(Frame& frame, const char* propertyName)
		{
			const char* value = frame.GetPropertyValue(propertyName);
			return value ? std::strtof(value, nullptr) : 0.0f;
		}
	}

	LineFrame::LineFrame(const std::string& type, const std::string& name)
		: Frame(type, name)
	{
		m_propConnections += AddProperty("Texture").Changed.connect(this, &LineFrame::OnTextureChanged);
		m_propConnections += AddProperty("StartX", "0").Changed.connect(this, &LineFrame::OnGeometryChanged);
		m_propConnections += AddProperty("StartY", "0").Changed.connect(this, &LineFrame::OnGeometryChanged);
		m_propConnections += AddProperty("EndX", "0").Changed.connect(this, &LineFrame::OnGeometryChanged);
		m_propConnections += AddProperty("EndY", "0").Changed.connect(this, &LineFrame::OnGeometryChanged);
		m_propConnections += AddProperty("Thickness", "4").Changed.connect(this, &LineFrame::OnGeometryChanged);
	}

	void LineFrame::Copy(Frame& other)
	{
		Frame::Copy(other);
		if (auto* line = dynamic_cast<LineFrame*>(&other))
		{
			line->m_texture = m_texture;
		}
	}

	void LineFrame::PopulateGeometryBuffer()
	{
		if (!m_texture)
		{
			return;
		}

		const Rect rect = GetAbsoluteFrameRect();
		// StartX/Y, EndX/Y and Thickness are authored in logical UI units. Anchor offsets are scaled by
		// the UI scale (see Anchor::ApplyToAbsRect), and the geometry here is built in absolute (scaled)
		// space relative to rect.left/top, so the endpoint offsets must be scaled the same way - otherwise
		// the connector lines drift away from the nodes whenever the UI scale is not 1.0.
		const auto uiScale = FrameManager::Get().GetUIScale();
		const float startX = rect.left + ReadFloat(*this, "StartX") * uiScale.x;
		const float startY = rect.top + ReadFloat(*this, "StartY") * uiScale.y;
		const float endX = rect.left + ReadFloat(*this, "EndX") * uiScale.x;
		const float endY = rect.top + ReadFloat(*this, "EndY") * uiScale.y;
		const float thickness = std::max(1.0f, ReadFloat(*this, "Thickness") * uiScale.y);
		const float length = std::sqrt((endX - startX) * (endX - startX) + (endY - startY) * (endY - startY));
		if (length <= 0.001f)
		{
			return;
		}

		const float normalX = -(endY - startY) / length * thickness * 0.5f;
		const float normalY = (endX - startX) / length * thickness * 0.5f;
		const argb_t color = GetColor().GetARGB();
		const GeometryBuffer::Vertex vertices[6] = {
			{{startX - normalX, startY - normalY, 0.0f}, color, {0.0f, 0.0f}},
			{{startX + normalX, startY + normalY, 0.0f}, color, {0.0f, 1.0f}},
			{{endX + normalX, endY + normalY, 0.0f}, color, {1.0f, 1.0f}},
			{{endX + normalX, endY + normalY, 0.0f}, color, {1.0f, 1.0f}},
			{{endX - normalX, endY - normalY, 0.0f}, color, {1.0f, 0.0f}},
			{{startX - normalX, startY - normalY, 0.0f}, color, {0.0f, 0.0f}}
		};

		GetGeometryBuffer().SetActiveTexture(m_texture);
		GetGeometryBuffer().AppendGeometry(vertices, 6);
	}

	void LineFrame::OnTextureChanged(const Property& property)
	{
		m_texture = property.GetValue().empty() ? nullptr : TextureManager::Get().CreateOrRetrieve(property.GetValue());
		Invalidate(false);
	}

	void LineFrame::OnGeometryChanged(const Property& property)
	{
		Invalidate(false);
	}
}
