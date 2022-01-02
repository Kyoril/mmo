// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <string>


namespace mmo
{
	/// This is the base class for a game state.
	class IGameState
		: public NonCopyable
	{
	public:
		/// Virtual default destructor.
		virtual ~IGameState() = default;

	public:
		/// Executed when the game state is entered.
		virtual void OnEnter() = 0;
		/// Executed when the game state is left.
		virtual void OnLeave() = 0;
		/// Gets the name of this game state.
		virtual const std::string& GetName() const = 0;
	};
}
