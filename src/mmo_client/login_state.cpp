// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_state.h"
#include "assets/asset_registry.h"

#include "base/macros.h"
#include "base/utilities.h"
#include "log/default_log_levels.h"
#include "frame_ui/frame_texture.h"
#include "frame_ui/frame_font_string.h"
#include "frame_ui/frame_mgr.h"
#include "graphics/texture_mgr.h"

#include "asio.hpp"

#include <set>


namespace mmo
{
	const std::string LoginState::Name = "login";

	static FrameManager &s_frameMgr = FrameManager::Get();


	void LoginState::OnEnter()
	{
		// Make the logo frame element
		auto topFrame = s_frameMgr.CreateOrRetrieve("Frame", "TopFrame");
		s_frameMgr.SetTopFrame(topFrame);

		// Load ui file
		s_frameMgr.LoadUIFile("Interface/GlueUI/GlueUI.toc");

		// Register drawing of the login ui
		m_paintLayer = Screen::AddLayer(std::bind(&LoginState::OnPaint, this), 1.0f, ScreenLayerFlags::IdentityTransform);
	}

	void LoginState::OnLeave()
	{
		// No longer draw current layer
		Screen::RemoveLayer(m_paintLayer);

		// Reset the logo frame ui
		s_frameMgr.ResetTopFrame();

		// Reset texture
		m_texture.reset();
	}

	const std::string & LoginState::GetName() const
	{
		return LoginState::Name;
	}

	void LoginState::OnPaint()
	{
		// Render the logo frame ui
		FrameManager::Get().Draw();
	}
}
