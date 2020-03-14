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
		explicit BorderComponent(Frame& frame, std::string filename, float borderInset);
		virtual ~BorderComponent() = default;

	public:
		virtual std::unique_ptr<FrameComponent> Copy() const override;

	public:
		// ~Begin FrameComponent
		void Render() const override;
		virtual Size GetSize() const override;
		// ~End FrameComponent

	private:
		std::string m_filename;
		/// The graphics texture object.
		TexturePtr m_texture;
		/// 
		Rect m_contentRect;
		/// Border inset.
		float m_borderInset;
	};
}
