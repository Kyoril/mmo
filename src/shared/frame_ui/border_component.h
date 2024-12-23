// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
		inline void SetBorderSize(const Rect borderSizeRect) { m_borderSizeRect = borderSizeRect; }

		void SetTint(argb_t tint);

		inline argb_t GetTint() const noexcept { return m_tint; }

	public:
		virtual std::unique_ptr<FrameComponent> Copy() const override;

	public:
		// ~Begin FrameComponent
		void Render(const Rect& area, const Color& color = Color::White) override;
		virtual Size GetSize() const override;
		// ~End FrameComponent

	private:
		std::string m_filename;
		/// The graphics texture object.
		TexturePtr m_texture;
		/// 
		Rect m_borderSizeRect;
		/// Border inset.
		float m_borderInset;
		/// Color tint.
		Color m_tint = Color::White;
	};
}
