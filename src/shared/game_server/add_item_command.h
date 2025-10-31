// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file add_item_command.h
 *
 * @brief Command for adding an item to the inventory.
 *
 * Encapsulates the logic for adding an existing item instance to the
 * inventory, including validation, slot finding, and state updates.
 */

#pragma once

#include "inventory_command.h"
#include "base/typedefs.h"

#include <memory>
#include <optional>

namespace mmo
{
	// Forward declarations
	class GameItemS;
	class ItemValidator;
	class SlotManager;

	/**
	 * @brief Context interface for AddItemCommand dependencies.
	 *
	 * Provides the minimal set of operations needed to add an item,
	 * following Dependency Inversion Principle.
	 */
	class IAddItemCommandContext
	{
	public:
		virtual ~IAddItemCommandContext() = default;

		/**
		 * @brief Adds item to a specific slot and updates all systems.
		 * @param item The item to add.
		 * @param slot Absolute slot coordinates.
		 */
		virtual void AddItemToSlot(
			std::shared_ptr<GameItemS> item,
			uint16 slot) = 0;

		/**
		 * @brief Gets the item validator service.
		 * @return Reference to validator.
		 */
		[[nodiscard]] virtual ItemValidator& GetValidator() = 0;

		/**
		 * @brief Gets the slot manager service.
		 * @return Reference to slot manager.
		 */
		[[nodiscard]] virtual SlotManager& GetSlotManager() = 0;
	};

	/**
	 * @brief Command to add an item to the inventory.
	 *
	 * This command encapsulates the complete operation of adding an item:
	 * 1. Find suitable slot (if not specified)
	 * 2. Validate item can be added to slot
	 * 3. Add item and update all related systems
	 *
	 * Usage:
	 * @code
	 * auto command = std::make_unique<AddItemCommand>(context, item);
	 * auto result = command->Execute();
	 * if (result.IsSuccess()) {
	 *     auto slot = result.GetValue();
	 *     // Item added successfully
	 * }
	 * @endcode
	 */
	class AddItemCommand final : public IInventoryCommand
	{
	public:
		/**
		 * @brief Constructs command to add item to any available slot.
		 * @param context Command execution context.
		 * @param item The item instance to add.
		 */
		AddItemCommand(
			IAddItemCommandContext& context,
			std::shared_ptr<GameItemS> item) noexcept;

		/**
		 * @brief Constructs command to add item to specific slot.
		 * @param context Command execution context.
		 * @param item The item instance to add.
		 * @param targetSlot Desired slot for the item.
		 */
		AddItemCommand(
			IAddItemCommandContext& context,
			std::shared_ptr<GameItemS> item,
			InventorySlot targetSlot) noexcept;

	public:
		// IInventoryCommand implementation
		[[nodiscard]] InventoryResult<void> Execute() override;
		[[nodiscard]] const char* GetDescription() const noexcept override;

		/**
		 * @brief Gets the slot where item was added (after Execute).
		 * @return Slot where item was placed, or nullopt if not yet executed.
		 */
		[[nodiscard]] std::optional<InventorySlot> GetResultSlot() const noexcept;

	private:
		/**
		 * @brief Finds suitable slot for the item.
		 * @return Slot where item can be added, or error.
		 */
		[[nodiscard]] InventoryResult<InventorySlot> FindTargetSlot();

		/**
		 * @brief Validates item can be added to the target slot.
		 * @param slot The target slot.
		 * @return Success or validation error.
		 */
		[[nodiscard]] InventoryResult<void> ValidateAddition(
			InventorySlot slot) const;

	private:
		IAddItemCommandContext& m_context;
		std::shared_ptr<GameItemS> m_item;
		std::optional<InventorySlot> m_targetSlot;
		std::optional<InventorySlot> m_resultSlot;
	};
}
