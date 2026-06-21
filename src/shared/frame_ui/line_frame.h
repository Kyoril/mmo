// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame.h"
#include "graphics/texture.h"

namespace mmo
{
	/// Renders a textured line between two points in the frame's local area.
	class LineFrame final : public Frame
	{
	public:
		static constexpr const char* Type = "Line";

		explicit LineFrame(const std::string& type, const std::string& name);

		void Copy(Frame& other) override;

	protected:
		void PopulateGeometryBuffer() override;

	private:
		void OnTextureChanged(const Property& property);
		void OnGeometryChanged(const Property& property);

	private:
		TexturePtr m_texture;
	};
}
