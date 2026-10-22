// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <cstring>

#include "terrain/water_mesh_build.h"

using namespace mmo;
using namespace mmo::terrain::water_mesh;

TEST_CASE("ShoreDepth_Terrain_Above_Surface_Encodes_As_Shoreline", "[water_mesh]")
{
	// The water edge itself, and terrain poking out of the water, must read as the shallowest
	// possible water rather than wrapping around to some large value.
	CHECK(EncodeShoreDepth(10.0f, 10.0f) == 0u);
	CHECK(EncodeShoreDepth(10.0f, 14.0f) == 0u);
}

TEST_CASE("ShoreDepth_Deep_Water_Saturates", "[water_mesh]")
{
	CHECK(EncodeShoreDepth(10.0f, 10.0f - ShoreDepthEncodeRange) == 255u);
	CHECK(EncodeShoreDepth(10.0f, -500.0f) == 255u);
}

TEST_CASE("ShoreDepth_Is_Linear_Across_The_Range", "[water_mesh]")
{
	// Half the range deep lands in the middle of the byte, so the material's linear decode
	// (red * ShoreDepthEncodeRange) recovers the depth.
	const uint32 half = EncodeShoreDepth(0.0f, -ShoreDepthEncodeRange * 0.5f);
	CHECK(half >= 127u);
	CHECK(half <= 128u);

	const float decoded = static_cast<float>(EncodeShoreDepth(0.0f, -2.0f)) / 255.0f * ShoreDepthEncodeRange;
	CHECK(decoded == Approx(2.0f).margin(ShoreDepthEncodeRange / 255.0f));
}

TEST_CASE("WaterVertexColor_Keeps_Face_Tag_And_Shore_Depth_Apart", "[water_mesh]")
{
	// Alpha is the face tag the material switches the underside look on; red is the shore depth.
	// Neither may leak into the other.
	const uint32 top = MakeWaterVertexColor(TopFaceVertexAlpha, 0.0f, -ShoreDepthEncodeRange);
	CHECK(((top >> 24) & 0xFFu) == TopFaceVertexAlpha);
	CHECK((top & 0xFFu) == 255u);
	CHECK(((top >> 8) & 0xFFFFu) == 0xFFFFu);

	const uint32 bottom = MakeWaterVertexColor(BottomFaceVertexAlpha, 0.0f, 0.0f);
	CHECK(((bottom >> 24) & 0xFFu) == BottomFaceVertexAlpha);
	CHECK((bottom & 0xFFu) == 0u);
}

TEST_CASE("WaterVertexColor_Puts_Shore_Depth_In_The_Shader_Red_Channel", "[water_mesh]")
{
	// Vertex colours upload as R8G8B8A8_UNORM: in memory (little endian) the first byte is the
	// shader's red. The water graphs and WaveShoreAttenuation read the depth from red.
	const uint32 color = MakeWaterVertexColor(TopFaceVertexAlpha, 0.0f, -2.0f);
	uint8 bytes[4];
	std::memcpy(bytes, &color, 4);
	CHECK(bytes[0] == EncodeShoreDepth(0.0f, -2.0f));
	CHECK(bytes[3] == TopFaceVertexAlpha);
}
