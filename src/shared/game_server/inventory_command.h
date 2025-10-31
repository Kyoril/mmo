// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file inventory_command.h
 *
 * @brief Base interface for inventory operation commands.
 *
 * Defines the Command Pattern interface for encapsulating inventory
 * operations, following Clean Architecture and enabling better testability,
 * operation composition, and potential undo/redo functionality.
 */

#pragma once

#include "inventory_types.h"

namespace mmo
{
	/**
	 * @brief Base interface for all inventory commands.
	 *
	 * The Command Pattern encapsulates inventory operations as first-class
	 * objects, providing several benefits:
	 * - Clear operation boundaries and single responsibility
	 * - Easy to test in isolation with mocked dependencies
	 * - Can be composed, queued, or logged
	 * - Foundation for undo/redo functionality
	 * - Explicit separation between command creation and execution
	 *
	 * Design follows the Command Pattern from Gang of Four, adapted for
	 * modern C++ with Result types instead of exceptions.
	 */
	class IInventoryCommand
	{
	public:
		virtual ~IInventoryCommand() = default;

		/**
		 * @brief Executes the inventory command.
		 *
		 * Performs the encapsulated operation, returning a Result that
		 * indicates success or the specific failure reason.
		 *
		 * @return InventoryResult indicating success or failure with reason.
		 */
		[[nodiscard]] virtual InventoryResult<void> Execute() = 0;

		/**
		 * @brief Gets a human-readable description of the command.
		 *
		 * Useful for logging, debugging, and potential UI display.
		 *
		 * @return String describing the command operation.
		 */
		[[nodiscard]] virtual const char* GetDescription() const noexcept = 0;
	};
}
