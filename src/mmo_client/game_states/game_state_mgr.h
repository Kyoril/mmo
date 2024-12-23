// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/utilities.h"
#include "base/signal.h"

#include "game_states/game_state.h"

#include <memory>
#include <map>

#include "ui/model_frame.h"


namespace mmo
{
	/// This class represents the game state manager. It manages all available game states
	/// as well as the current game state.
	class GameStateMgr final
		: public NonCopyable
	{
	private:
		/// Private constructor since this is a singleton class.
		GameStateMgr();

	public:
		/// Adds a new game state to the list of available game states.
		void AddGameState(std::shared_ptr<GameState> gameState);
		/// Removes a game state from the list of available game states.
		void RemoveGameState(const std::string& name);
		/// Destroys the game state manager.
		void RemoveAllGameStates();
		/// Sets the current game state.
		void SetGameState(const std::string& name);

		void Idle(float deltaSeconds, GameTime timestamp);
	
	public:
		/// Gets the global 
		static GameStateMgr& Get();

	private:
		/// Contains all available game states.
		std::map<std::string, std::shared_ptr<GameState>, StrCaseIComp> m_gameStates;
		/// The current game state.
		std::shared_ptr<GameState> m_currentState;
		/// The pending game state.
		std::weak_ptr<GameState> m_pendingState;
		scoped_connection m_idleConnection;
	};
}
