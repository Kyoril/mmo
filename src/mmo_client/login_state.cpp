// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "login_state.h"


namespace mmo
{
	const std::string LoginState::Name = "login";


	void LoginState::OnEnter()
	{

	}

	void LoginState::OnLeave()
	{

	}

	const std::string & LoginState::GetName() const
	{
		return LoginState::Name;
	}
}
