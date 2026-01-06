// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <memory>
#include <string>

namespace mmo
{
	// Forward declarations
	class BotContext;

	/// Result of executing a bot action.
	enum class ActionResult
	{
		/// The action completed successfully and the next action can be executed.
		Success,
		
		/// The action is still in progress and should be executed again on the next update.
		InProgress,
		
		/// The action failed and the bot should handle the error (e.g., stop execution, retry, etc.).
		Failed
	};

	/// Abstract interface for bot actions following the Command pattern.
	/// Each action encapsulates a single behavior that a bot can perform,
	/// such as moving, chatting, casting a spell, or waiting.
	/// 
	/// Actions can be:
	/// - Instantaneous (complete in one execution)
	/// - Asynchronous (require multiple executions to complete)
	/// - Conditional (check preconditions before execution)
	class IBotAction
	{
	public:
		virtual ~IBotAction() = default;

		/// Returns a human-readable description of the action for logging and debugging.
		virtual std::string GetDescription() const = 0;

		/// Executes the action using the provided bot context.
		/// 
		/// @param context The bot context providing access to game state and operations.
		/// @return The result of the action execution.
		virtual ActionResult Execute(BotContext& context) = 0;

		/// Called when the action is aborted or the bot is shutting down.
		/// Allows the action to clean up resources or reset state.
		virtual void OnAbort(BotContext& context)
		{
			// Default implementation does nothing
		}

		/// Checks if the action can be executed given the current context.
		/// This allows actions to validate preconditions before execution.
		/// 
		/// @param context The bot context to check against.
		/// @param outReason If validation fails, this string will contain the reason.
		/// @return True if the action can be executed, false otherwise.
		virtual bool CanExecute(const BotContext& context, std::string& outReason) const
		{
			// By default, all actions can be executed
			return true;
		}

		/// Checks if this action can be interrupted by urgent actions (e.g., event handlers).
		/// Interruptible actions (like waiting) will be aborted when an urgent action is queued.
		/// Non-interruptible actions (like sending a chat message) will complete first.
		/// @return True if this action can be interrupted, false otherwise.
		virtual bool IsInterruptible() const
		{
			// By default, actions are not interruptible to ensure atomic execution
			return false;
		}
	};

	/// Base class for bot actions providing common functionality.
	/// Derived classes only need to implement Execute() and GetDescription().
	class BotAction : public IBotAction
	{
	public:
		virtual ~BotAction() = default;
	};

	/// Shared pointer type for bot actions.
	using BotActionPtr = std::shared_ptr<IBotAction>;
}
