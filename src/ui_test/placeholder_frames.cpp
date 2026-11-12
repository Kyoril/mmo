// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "placeholder_frames.h"

#include "frame_ui/default_renderer.h"
#include "frame_ui/frame_mgr.h"

#include "luabind/luabind.hpp"

namespace mmo
{
	void RegisterPlaceholderFrames(lua_State* luaState)
	{
		FrameManager& frameMgr = FrameManager::Get();

		// Renderers of 3D content: the frame's own imagery still renders, the 3D part is left out.
		for (const char* rendererName : { "ModelRenderer", "WorldRenderer" })
		{
			frameMgr.RegisterFrameRenderer(rendererName, [](const std::string& name)
				{
					return std::make_unique<DefaultRenderer>(name);
				});
		}

		frameMgr.RegisterFrameFactory("Model", [](const std::string& name)
			{
				return std::make_shared<PlaceholderModelFrame>("Model", name);
			});
		frameMgr.RegisterFrameFactory("CharacterPreview", [](const std::string& name)
			{
				return std::make_shared<PlaceholderModelFrame>("CharacterPreview", name);
			});
		frameMgr.RegisterFrameFactory("UnitModel", [](const std::string& name)
			{
				return std::make_shared<PlaceholderUnitModelFrame>(name);
			});
		frameMgr.RegisterFrameFactory("Cooldown", [](const std::string& name)
			{
				return std::make_shared<PlaceholderCooldownFrame>(name);
			});
		frameMgr.RegisterFrameFactory("Minimap", [](const std::string& name)
			{
				return std::make_shared<Frame>("Minimap", name);
			});
		frameMgr.RegisterFrameFactory("WorldMap", [](const std::string& name)
			{
				return std::make_shared<Frame>("WorldMap", name);
			});
		frameMgr.RegisterFrameFactory("World", [](const std::string& name)
			{
				return std::make_shared<Frame>("World", name);
			});

		// Same Lua class names and methods as the client registers in ClientUiRuntime.
		LUABIND_MODULE(luaState,
			luabind::scope(
				luabind::class_<PlaceholderModelFrame, Frame>("ModelFrame")
				.def("SetModelFile", &PlaceholderModelFrame::SetModelFile)
				.def("Yaw", &PlaceholderModelFrame::Yaw)
				.def("SetZoom", &PlaceholderModelFrame::SetZoom)
				.def("GetZoom", &PlaceholderModelFrame::GetZoom)
				.def("GetYaw", &PlaceholderModelFrame::GetYaw)
				.def("ResetYaw", &PlaceholderModelFrame::ResetYaw)
				.def("InvalidateModel", &PlaceholderModelFrame::InvalidateModel)
				.def("SetAutoRender", &PlaceholderModelFrame::SetAutoRender)),

			luabind::scope(
				luabind::class_<PlaceholderUnitModelFrame, PlaceholderModelFrame>("UnitModelFrame")
				.def("SetUnit", &PlaceholderUnitModelFrame::SetUnit)),

			luabind::scope(
				luabind::class_<PlaceholderCooldownFrame, Frame>("CooldownFrame")
				.def("SetProgress", &PlaceholderCooldownFrame::SetProgress)
				.def("GetProgress", &PlaceholderCooldownFrame::GetProgress)));
	}
}
