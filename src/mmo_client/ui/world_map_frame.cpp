// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_map_frame.h"

#include "world_map.h"

#include "frame_ui/font_mgr.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/geometry_helper.h"

#include <cmath>

namespace mmo
{
	namespace
	{
		FontPtr LoadFontMap(const char* name)
		{
			const FrameManager::FontMap* fontMap = FrameManager::Get().GetFontMap(name);
			if (!fontMap)
			{
				return nullptr;
			}

			return FontManager::Get().CreateOrRetrieve(fontMap->FontFile, fontMap->Size, fontMap->Outline, fontMap->ShadowX, fontMap->ShadowY);
		}
	}

	WorldMapFrame::WorldMapFrame(const String& name, WorldMap& worldMap)
		: Frame("WorldMap", name)
		, m_worldMap(worldMap)
	{
	}

	void WorldMapFrame::EnsureFonts()
	{
		if (m_fontsResolved)
		{
			return;
		}

		m_fontsResolved = true;
		m_worldMap.SetFonts(LoadFontMap("WorldMapTitleFont"), LoadFontMap("WorldMapZoneFont"), LoadFontMap("WorldMapSubZoneFont"));
	}

	void WorldMapFrame::DrawSelf()
	{
		if (IsVisible(false))
		{
			EnsureFonts();

			const Rect rect = GetAbsoluteFrameRect();
			const auto width = static_cast<uint16>(std::max(1.0f, std::round(rect.GetWidth())));
			const auto height = static_cast<uint16>(std::max(1.0f, std::round(rect.GetHeight())));
			m_worldMap.Render(width, height, FrameManager::Get().GetTextScale());

			// The render texture is recreated when the frame changes size
			if (m_worldMap.GetTexture().get() != m_boundTexture)
			{
				m_boundTexture = m_worldMap.GetTexture().get();
				m_needsRedraw = true;
			}
		}

		Frame::DrawSelf();
	}

	void WorldMapFrame::PopulateGeometryBuffer()
	{
		const RenderTexturePtr& texture = m_worldMap.GetTexture();
		if (!texture)
		{
			return;
		}

		Color color = m_color;
		color.SetAlpha(GetOpacity(true));
		m_geometryBuffer.SetActiveTexture(texture);
		GeometryHelper::CreateRect(m_geometryBuffer, color, GetAbsoluteFrameRect(), Rect(0.0f, 0.0f, 1.0f, 1.0f), 1, 1);
	}

	void WorldMapFrame::Update(const float elapsed)
	{
		if (IsVisible(true))
		{
			m_worldMap.Update(elapsed);
		}

		Frame::Update(elapsed);
	}

	Point WorldMapFrame::ToLocal(const Point& position)
	{
		const Rect rect = GetAbsoluteFrameRect();
		return Point(position.x - rect.left, position.y - rect.top);
	}

	bool WorldMapFrame::OnMouseDown(const MouseButton button, const int32 buttons, const Point& position)
	{
		const Point local = ToLocal(position);
		m_worldMap.OnMouseDown(button, local.x, local.y);
		Frame::OnMouseDown(button, buttons, position);
		return true;
	}

	bool WorldMapFrame::OnMouseUp(const MouseButton button, const int32 buttons, const Point& position)
	{
		const Point local = ToLocal(position);
		m_worldMap.OnMouseUp(button, local.x, local.y);
		Frame::OnMouseUp(button, buttons, position);
		return true;
	}

	void WorldMapFrame::OnMouseMoved(const Point& position, const Point& delta)
	{
		const Point local = ToLocal(position);
		m_worldMap.OnMouseMoved(local.x, local.y);
		Frame::OnMouseMoved(position, delta);
	}

	bool WorldMapFrame::OnMouseWheel(const int32 delta)
	{
		m_worldMap.OnMouseWheel(delta);
		Frame::OnMouseWheel(delta);
		return true;
	}

	void WorldMapFrame::OnMouseLeave()
	{
		m_worldMap.OnMouseLeave();
		Frame::OnMouseLeave();
	}

	void WorldMapFrame::OnHide()
	{
		m_worldMap.ReleaseTiles();
		Frame::OnHide();
	}
}
