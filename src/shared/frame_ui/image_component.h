// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_component.h"
#include "base/signal.h"

#include "base/typedefs.h"
#include "graphics/texture.h"


namespace mmo
{
	enum class ImageTilingMode : uint8
	{
		None,

		Horizontally,
		Vertically,

		Both
	};

	/// Parses a string and converts it to a ImageTilingMode enum value.
	ImageTilingMode ImageTilingModeByName(const std::string& name);
	/// Generates the name of a ImageTilingMode enum value.
	std::string ImageTilingModeName(ImageTilingMode alignment);


	/// This class is a texture frame object which can be used to render plain images.
	class ImageComponent final
		: public FrameComponent
	{
	public:
		/// Creates a frame texture object from a texture file. The texture manager class
		/// is used to avoid loading textures twice.
		explicit ImageComponent(Frame& frame, const std::string& filename);

	public:
		std::unique_ptr<FrameComponent> Copy() const override;

	protected:
		void OnFrameChanged() override;

	public:
		// FrameComponent overrides
		void Render(const Rect& area, const Color& color = Color::White) override;

	public:
		/// Sets the tiling mode for this component.
		void SetTilingMode(ImageTilingMode mode);
		void SetTint(argb_t tint);
		inline argb_t GetTint() const noexcept { return m_tint; }
		void SetImageFile(const std::string& filename);
		void SetImagePropertyName(std::string propertyName);

	public:
		// ~Begin FrameComponent
		Size GetSize() const override;
		// ~End FrameComponent

	private:
		std::string m_filename;
		/// The graphics texture object.
		TexturePtr m_texture;
		/// The width at which the frame texture object is drawn. If set to 0, the texture width is used.
		uint16 m_width;
		/// The height at which the frame texture object is drawn. If set to 0, the texture height is used.
		uint16 m_height;
		/// The tiling mode of the image.
		ImageTilingMode m_tiling;
		/// Color tint.
		Color m_tint = Color::White;

		std::string m_propertyName;

		scoped_connection_container m_propertyConnection;
	};
}
