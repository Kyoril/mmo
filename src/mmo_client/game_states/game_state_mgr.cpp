// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_state_mgr.h"
#include "event_loop.h"

#include "base/macros.h"


namespace mmo
{
	GameStateMgr::GameStateMgr()
	{
		m_idleConnection = EventLoop::Idle.connect(*this, &GameStateMgr::Idle);
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

	void GameStateMgr::AddGameState(const std::shared_ptr<GameState> gameState)
	{
		ASSERT(gameState);
		ASSERT(m_gameStates.find(String(gameState->GetName())) == m_gameStates.end());

		m_gameStates[String(gameState->GetName())] = gameState;
	}

	void GameStateMgr::RemoveGameState(const std::string & name)
	{
		const auto gameStateIt = m_gameStates.find(name);
		ASSERT(gameStateIt != m_gameStates.end());
		ASSERT(gameStateIt->second != m_currentState);
		
		m_gameStates.erase(gameStateIt);
	}

	void GameStateMgr::SetGameState(const std::string & name)
	{
		const auto it = m_gameStates.find(name);
		ASSERT(it != m_gameStates.end());

		if (!m_currentState)
		{
			m_currentState = it->second;
			m_currentState->OnEnter();
		}
		else
		{
			m_pendingState = it->second;	
		}
	}

	void GameStateMgr::Idle(float deltaSeconds, GameTime timestamp)
	{
		auto pendingState = m_pendingState.lock();
		m_pendingState.reset();
			
		if (pendingState)
		{
			if (m_currentState)
			{
				m_currentState->OnLeave();
			}

			m_currentState = std::move(pendingState);
			m_currentState->OnEnter();
		}
	}

	GameStateMgr & GameStateMgr::Get()
	{
		static GameStateMgr gameStateMgr;
		return gameStateMgr;
	}
}
