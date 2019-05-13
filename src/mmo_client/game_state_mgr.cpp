// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "game_state_mgr.h"

#include "base/macros.h"


namespace mmo
{
	GameStateMgr::GameStateMgr()
	{
		// 
	}

	void GameStateMgr::RemoveAllGameStates()
	{
		if (m_currentState)
		{
			m_currentState->OnLeave();
			m_currentState = nullptr;
		}

		m_gameStates.clear();
	}

	void GameStateMgr::AddGameState(std::shared_ptr<IGameState> gameState)
	{
		ASSERT(gameState);
		ASSERT(m_gameStates.find(gameState->GetName()) == m_gameStates.end());

		m_gameStates[gameState->GetName()] = gameState;
	}

	void GameStateMgr::RemoveGameState(const std::string & name)
	{
		ASSERT(m_gameStates.find(name) != m_gameStates.end());
		m_gameStates.erase(m_gameStates.find(name));
	}

	void GameStateMgr::SetGameState(const std::string & name)
	{
		ASSERT(m_gameStates.find(name) != m_gameStates.end());
		
		if (m_currentState)
		{
			m_currentState->OnLeave();
		}

		m_currentState = m_gameStates.find(name)->second;
		m_currentState->OnEnter();
	}

	GameStateMgr & GameStateMgr::Get()
	{
		static GameStateMgr s_gameStateMgr;
		return s_gameStateMgr;
	}
}
