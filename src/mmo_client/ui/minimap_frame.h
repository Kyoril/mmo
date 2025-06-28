#pragma once

#include "frame_ui/frame.h"

namespace mmo
{
	class Minimap;

	class MinimapFrame : public Frame
	{
	public:
		MinimapFrame(const String& name, Minimap& minimap);

	public:
		void DrawSelf() override;

		void PopulateGeometryBuffer() override;

	private:
		Minimap& m_minimap;
	};
}
