// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_context.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace mmo
{
	/// Interface for bot behavior profiles.
	/// A profile defines the behavior and actions a bot should perform.
	/// Different profiles can implement different strategies (e.g., idle bot,
	/// wandering bot, combat bot, testing bot, etc.).
	/// 
	/// Following the Strategy pattern, profiles can be swapped at runtime
	/// to change bot behavior without modifying the bot's core logic.
	class IBotProfile
	{
	public:
		virtual ~IBotProfile() = default;

		/// Returns the name of this profile.
		virtual std::string GetName() const = 0;

		/// Called when the profile is activated (bot enters world).
		/// This is where the profile should initialize its state and queue initial actions.
		virtual void OnActivate(BotContext& context) = 0;

		/// Called periodically to update the profile and execute queued actions.
		/// @param context The bot context.
		/// @return True if the profile should continue running, false if it's done.
		virtual bool Update(BotContext& context) = 0;

		/// Called when the profile is deactivated (bot leaves world or switches profile).
		virtual void OnDeactivate(BotContext& context) = 0;
	};

	/// Base implementation of a bot profile that manages an action queue.
	/// Derived classes can add actions to the queue and let this class handle execution.
	class BotProfile : public IBotProfile
	{
	public:
		virtual ~BotProfile() = default;

		void OnActivate(BotContext& context) override
		{
			// Clear any existing actions
			m_actionQueue.clear();
			m_currentAction.reset();
			
			// Let derived class initialize
			OnActivateImpl(context);
		}

		bool Update(BotContext& context) override
		{
			// If we have a current action, execute it
			if (m_currentAction)
			{
				const ActionResult result = m_currentAction->Execute(context);
				
				switch (result)
				{
					case ActionResult::Success:
						// Action completed, move to next
						m_currentAction.reset();
						break;
						
					case ActionResult::InProgress:
						// Action still running, continue on next update
						return true;
						
					case ActionResult::Failed:
						// Action failed, handle error
						OnActionFailed(context, m_currentAction);
						m_currentAction.reset();
						break;
				}
			}

			// If no current action and queue is empty, let derived class decide what to do
			if (!m_currentAction && m_actionQueue.empty())
			{
				return OnQueueEmpty(context);
			}

			// Pop next action from queue
			if (!m_currentAction && !m_actionQueue.empty())
			{
				m_currentAction = m_actionQueue.front();
				m_actionQueue.pop_front();
				
				// Validate the action can be executed
				std::string failureReason;
				if (!m_currentAction->CanExecute(context, failureReason))
				{
					WLOG("Cannot execute action '" << m_currentAction->GetDescription()
						<< "': " << failureReason);
					m_currentAction.reset();
					return true;
				}

				ILOG("Executing action: " << m_currentAction->GetDescription());
			}

			return true;
		}

		void OnDeactivate(BotContext& context) override
		{
			// Abort current action if any
			if (m_currentAction)
			{
				m_currentAction->OnAbort(context);
				m_currentAction.reset();
			}

			// Clear queue
			m_actionQueue.clear();

			// Let derived class clean up
			OnDeactivateImpl(context);
		}

	protected:
		/// Called by OnActivate to let derived classes initialize.
		virtual void OnActivateImpl(BotContext& context) = 0;

		/// Called by OnDeactivate to let derived classes clean up.
		virtual void OnDeactivateImpl(BotContext& context)
		{
			// Default implementation does nothing
		}

		/// Called when the action queue is empty.
		/// Derived classes can override this to queue more actions or return false to stop.
		/// @return True to continue running, false to stop the profile.
		virtual bool OnQueueEmpty(BotContext& context)
		{
			// By default, stop when queue is empty
			return false;
		}

		/// Called when an action fails.
		/// Derived classes can override this to handle errors (e.g., retry, skip, stop).
		virtual void OnActionFailed(BotContext& context, const BotActionPtr& action)
		{
			WLOG("Action failed: " << action->GetDescription());
		}

		/// Adds an action to the end of the queue.
		void QueueAction(BotActionPtr action)
		{
			m_actionQueue.push_back(std::move(action));
		}

		/// Adds an action to the front of the queue (will be executed next).
		void QueueActionNext(BotActionPtr action)
		{
			m_actionQueue.push_front(std::move(action));
		}

		/// Adds multiple actions to the queue.
		void QueueActions(const std::vector<BotActionPtr>& actions)
		{
			for (const auto& action : actions)
			{
				QueueAction(action);
			}
		}

		/// Clears all queued actions.
		void ClearQueue()
		{
			m_actionQueue.clear();
		}

		/// Gets the number of queued actions.
		size_t GetQueueSize() const
		{
			return m_actionQueue.size();
		}

	private:
		std::deque<BotActionPtr> m_actionQueue;
		BotActionPtr m_currentAction;
	};

	/// Shared pointer type for bot profiles.
	using BotProfilePtr = std::shared_ptr<IBotProfile>;
}
