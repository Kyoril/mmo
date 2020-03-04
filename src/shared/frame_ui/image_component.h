// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_component.h"

#include "graphics/texture.h"


namespace mmo
{
	/// This class is a texture frame object which can be used to render plain images.
	class ImageComponent 
		: public FrameComponent
	{
	public:
		/// Creates a frame texture object from a texture file. The texture manager class
		/// is used to avoid loading textures twice.
		explicit ImageComponent(const std::string& filename);
		virtual ~ImageComponent() = default;

	public:
		// FrameComponent overrides
		void Render(Frame& frame) const override;

	public:
		// ~Begin FrameComponent
		virtual Size GetSize() const override;
		// ~End FrameComponent

	private:
		/// The graphics texture object.
		TexturePtr m_texture;
		/// The width at which the frame texture object is drawn. If set to 0, the texture width is used.
		uint16 m_width;
		/// The height at which the frame texture object is drawn. If set to 0, the texture height is used.
		uint16 m_height;
	};
}
