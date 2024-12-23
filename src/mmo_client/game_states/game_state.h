// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <string>

namespace mmo
{
	class GameStateMgr;

	/// This is the base class for a game state.
	class GameState
		: public NonCopyable
	{
	public:
		/// @brief Default constructor.
		/// @param gameStateManager The game state manager that this game state belongs to.
		explicit GameState(GameStateMgr& gameStateManager) noexcept
			: m_gameStateMgr(gameStateManager)
		{
		}

		/// @brief Virtual default destructor because of inheritance.
		~GameState() override = default;

	public:
		/// @brief Executed when the game state is entered.
		virtual void OnEnter() = 0;

		/// @brief Executed when the game state is left.
		virtual void OnLeave() = 0;

		/// @brief Gets the name of this game state.
		///	@return Name of this game state.
		[[nodiscard]] virtual std::string_view GetName() const = 0;

		/// @brief Gets the game state manager that this game state belongs to.
		///	@return Game state manager that this game state belongs to.
		[[nodiscard]] GameStateMgr& GetGameStateManager() const noexcept { return m_gameStateMgr; }

	protected:
		GameStateMgr& m_gameStateMgr;
	};
}
