// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "game_state.h"


namespace mmo
{
	/// This class represents the initial game state where the player is asked to enter
	/// his credentials in order to authenticate.
	class LoginState final
		: public IGameState
	{
	public:
		/// The default name of the login state
		static const std::string Name;

	public:
		// Inherited via IGameState
		virtual void OnEnter() override;
		virtual void OnLeave() override;
		virtual const std::string & GetName() const override;

	};
}
