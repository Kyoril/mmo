// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_object.h"

#include "graphics/texture.h"


namespace mmo
{
	/// This class is a texture frame object which can be used to render plain images.
	class FrameTexture : public FrameObject
	{
	public:
		/// Creates a frame texture object from a texture file. The texture manager class
		/// is used to avoid loading textures twice.
		explicit FrameTexture(const std::string& filename);

	public:
		// FrameObject overrides
		void Render(GeometryBuffer& buffer) const override;

	private:
		/// The graphics texture object.
		TexturePtr m_texture;
		/// The width at which the frame texture object is drawn. If set to 0, the texture width is used.
		uint16 m_width;
		/// The height at which the frame texture object is drawn. If set to 0, the texture height is used.
		uint16 m_height;
	};
}
