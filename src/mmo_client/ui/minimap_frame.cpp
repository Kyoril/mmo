
#include "minimap_frame.h"

#include "minimap.h"
#include "frame_ui/geometry_helper.h"

namespace mmo
{
	MinimapFrame::MinimapFrame(const String& name, Minimap& minimap)
		: Frame("Minimap", name)
		, m_minimap(minimap)
	{
	}

	void MinimapFrame::PopulateGeometryBuffer()
	{
		TexturePtr texture = m_minimap.GetMinimapTexture();
		m_geometryBuffer.SetActiveTexture(texture);

		const Rect frameRect = GetAbsoluteFrameRect();

		// Default source rect encapsulates the whole image area
		Rect srcRect{ 0.0f, 0.0f, static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight()) };

		// Create the rectangle
		GeometryHelper::CreateRect(m_geometryBuffer,
			m_color,
			frameRect,
			srcRect,
			texture->GetWidth(), texture->GetHeight());
	}
}
