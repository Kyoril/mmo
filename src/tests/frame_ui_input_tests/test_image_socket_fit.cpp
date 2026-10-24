// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "frame_ui/frame.h"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/geometry_buffer.h"
#include "frame_ui/image_component.h"
#include "frame_ui/localization.h"
#include "graphics/texture_mgr.h"
#include "lua.hpp"

using namespace mmo;

TEST_CASE("Socket artwork scales its padding and clips its corners", "[frame_ui][image_socket]")
{
	if (!GraphicsDevice::HasInstance())
	{
		GraphicsDevice::CreateNull(GraphicsDeviceDesc());
	}
	Localization localization;
	lua_State* lua = luaL_newstate();
	luaL_openlibs(lua);
	luabind::open(lua);
	FrameManager::Initialize(lua, localization);
	{
		Frame frame("Frame", "SocketFitTest");
		auto texture = TextureManager::Get().CreateManual("test/socket_fit", 128, 128,
			PixelFormat::R8G8B8A8, BufferUsage::Static);
		ImageComponent image(frame, "test/socket_fit");

		SECTION("Existing images retain their rectangular rendering")
		{
			const Rect bounds(10, 20, 138, 148);
			CHECK(image.GetArea(bounds) == bounds);
			image.Render(bounds);
			CHECK(frame.GetGeometryBuffer().GetVertexCount() == 6);
		}

		SECTION("Padding remains proportional at inventory and character sizes")
		{
			image.SetRelativeInset(0.125f);
			image.SetCornerCut(0.08f);
			for (const float size : { 128.0f, 112.0f, 102.4f, 51.2f })
			{
				const Rect area = image.GetArea(Rect(10, 20, 10 + size, 20 + size));
				CHECK(area.left == Approx(10 + size * 0.125f));
				CHECK(area.top == Approx(20 + size * 0.125f));
				CHECK(area.GetWidth() == Approx(size * 0.75f));
				CHECK(area.GetHeight() == Approx(size * 0.75f));
			}
			// Template clones must retain both padding and the corner silhouette.
			auto clone = image.Copy();
			CHECK(clone->GetArea(Rect(0, 0, 128, 128)) == Rect(16, 16, 112, 112));
			clone->Render(Rect(0, 0, 128, 128));
			CHECK(frame.GetGeometryBuffer().GetVertexCount() == 24);
		}

		SECTION("Invalid padding cannot produce inverted geometry")
		{
			image.SetRelativeInset(1.0f);
			CHECK(image.GetArea(Rect(0, 0, 100, 100)).GetWidth() > 0.0f);
			image.SetRelativeInset(-1.0f);
			CHECK(image.GetArea(Rect(0, 0, 100, 100)) == Rect(0, 0, 100, 100));
		}
	}
	TextureManager::Get().ReleaseIfUnreferenced("test/socket_fit");
	FrameManager::Destroy();
	lua_close(lua);
}
