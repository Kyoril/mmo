// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_ui/frame.h"

namespace mmo
{
	class WorldMap;

	/// @brief Frame type "WorldMap": shows the world map and passes mouse input to it.
	/// @details Uses the font maps WorldMapTitleFont, WorldMapZoneFont and WorldMapSubZoneFont for the map's
	///	         labels; define them in the UI's XML.
	class WorldMapFrame final : public Frame
	{
	public:
		WorldMapFrame(const String& name, WorldMap& worldMap);

	public:
		void DrawSelf() override;

		void PopulateGeometryBuffer() override;

		void Update(float elapsed) override;

		bool OnMouseDown(MouseButton button, int32 buttons, const Point& position) override;

		bool OnMouseUp(MouseButton button, int32 buttons, const Point& position) override;

		void OnMouseMoved(const Point& position, const Point& delta) override;

		bool OnMouseWheel(int32 delta) override;

		void OnMouseLeave() override;

		void OnHide() override;

	private:
		/// @brief Converts a screen position to pixels relative to the frame.
		Point ToLocal(const Point& position);

		void EnsureFonts();

	private:
		WorldMap& m_worldMap;
		bool m_fontsResolved = false;
		const void* m_boundTexture = nullptr;
	};
}
