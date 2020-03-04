// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_component.h"

#include "graphics/texture.h"


namespace mmo
{
	/// Works like ImageComponent but treats outer pixels as a border so that it 
	/// isn't simply stretched.
	class BorderComponent
		: public FrameComponent
	{
	public:
		explicit BorderComponent(const std::string& filename, float borderInset);
		virtual ~BorderComponent() = default;

	public:
		// ~Begin FrameComponent
		void Render(Frame& frame) const override;
		virtual Size GetSize() const override;
		// ~End FrameComponent

	private:
		/// The graphics texture object.
		TexturePtr m_texture;
		/// 
		Rect m_contentRect;
	};
}
