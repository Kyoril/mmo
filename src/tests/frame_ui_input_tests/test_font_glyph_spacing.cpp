// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "assets/asset_registry.h"
#include "frame_ui/font.h"
#include "frame_ui/geometry_buffer.h"
#include "graphics/graphics_device.h"

using namespace mmo;

TEST_CASE("Scaled font geometry keeps digit spacing proportional to the font", "[font][glyph_spacing]")
{
	if (!GraphicsDevice::HasInstance())
	{
		GraphicsDevice::CreateNull(GraphicsDeviceDesc());
	}
	AssetRegistry::Initialize(MMO_TEST_CLIENT_DATA_DIR, {});
	const Point origin(50.0f, 20.0f);
	for (const auto* locale : {"enUS", "deDE", "frFR", "ruRU"})
	{
		Font font;
		const std::string path = std::string("Locales/Locale_") + locale + "/Fonts/FRIZQT__.TTF";
		REQUIRE(font.Initialize(path, 26.0f, 0.0f));
		INFO("Locale: " << locale);
		REQUIRE(font.GetGlyphData('1')->GetImage()->GetOffsetX() > 0.0f);
		GeometryBuffer reference;
		font.DrawText("1 11 12 21 101% A1A", origin, reference, 1.0f);
		REQUIRE(reference.GetVertexCount() > 0);

		for (const float scale : {0.45f, 0.5f, 0.8f, 1.0f, 1.25f})
		{
			INFO("Scale: " << scale);
			for (const bool bounded : {false, true})
			{
				INFO("Rectangle overload: " << bounded);
				GeometryBuffer scaled;
				if (bounded)
				{
					font.DrawText("1 11 12 21 101% A1A", Rect(origin, Size(2000.0f, 500.0f)), &scaled, scale);
				}
				else
				{
					font.DrawText("1 11 12 21 101% A1A", origin, scaled, scale);
				}
				REQUIRE(scaled.GetVertexCount() == reference.GetVertexCount());
				for (size_t i = 0; i < scaled.GetVertices().size(); ++i)
				{
					const float expectedX = origin.x + (reference.GetVertices()[i].pos.x - origin.x) * scale;
					CHECK(scaled.GetVertices()[i].pos.x == Approx(expectedX).margin(0.001f));
				}
			}
		}
	}
	AssetRegistry::Destroy();
}
