// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_component.h"

#include "base/signal.h"
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

		inline argb_t GetTint() const { return m_tint; }

		/// Binds the tint color to a frame property so that changing the property recolors
		/// the border at runtime. Pass an empty string to unbind.
		void SetTintPropertyName(std::string propertyName);

	public:
		virtual std::unique_ptr<FrameComponent> Copy() const override;

	public:
		// ~Begin FrameComponent
		void OnFrameChanged() override;
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
		/// Name of the frame property the tint is bound to, if any.
		std::string m_tintPropertyName;
		/// Connection to the bound property's change signal.
		scoped_connection m_tintPropertyConnection;
	};
}
