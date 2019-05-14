// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_state.h"

#include "log/default_log_levels.h"


namespace mmo
{
	const std::string LoginState::Name = "login";


	void LoginState::OnEnter()
	{
		// Register drawing of the login ui
		m_paintLayer = Screen::AddLayer(std::bind(&LoginState::OnPaint, this), 1.0f, 0);
	}

	void LoginState::OnLeave()
	{
		Screen::RemoveLayer(m_paintLayer);
	}

	const std::string & LoginState::GetName() const
	{
		return LoginState::Name;
	}

	void LoginState::OnPaint()
	{

	}
}
