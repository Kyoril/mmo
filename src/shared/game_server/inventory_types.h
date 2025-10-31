// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

/**
 * @file inventory_types.h
 *
 * @brief Strong types for inventory system to replace primitive obsession.
 *
 * This file provides value objects and strong types that encapsulate inventory
 * domain concepts, making the code more type-safe and self-documenting while
 * reducing primitive obsession anti-pattern.
 */

#pragma once

#include "base/typedefs.h"
#include "game/item.h"

#include <optional>
#include <functional>

namespace mmo
{
	/**
	 * @brief Represents an inventory slot position with strong typing.
	 *
	 * This class encapsulates slot addressing logic, providing type safety
	 * and preventing mixing of absolute and relative slot coordinates.
	 * It follows the value object pattern - immutable and equality comparable.
	 *
	 * Slot encoding: Absolute slot = (bag << 8) | slot
	 * - Bag 0xFF (255) is the player's main inventory (equipment + backpack)
	 * - Bags 19-22 are equipped bag containers
	 * - Slot positions vary by bag type
	 */
	class InventorySlot final
	{
	public:
		/**
		 * @brief Creates an inventory slot from an absolute slot value.
		 * @param absolute The absolute slot value (bag and slot encoded).
		 * @return InventorySlot instance.
		 */
		static InventorySlot FromAbsolute(uint16 absolute);

		/**
		 * @brief Creates an inventory slot from relative bag and slot coordinates.
		 * @param bag The bag index (255 for main inventory, 19-22 for equipped bags).
		 * @param slot The slot within the bag.
		 * @return InventorySlot instance.
		 */
		static InventorySlot FromRelative(uint8 bag, uint8 slot);

		/**
		 * @brief Gets the absolute slot value.
		 * @return Absolute slot encoded as (bag << 8) | slot.
		 */
		uint16 GetAbsolute() const noexcept
		{
			return m_absolute;
		}

		/**
		 * @brief Gets the bag portion of the slot address.
		 * @return The bag index.
		 */
		uint8 GetBag() const noexcept
		{
			return static_cast<uint8>(m_absolute >> 8);
		}

		/**
		 * @brief Gets the slot portion of the slot address.
		 * @return The slot index within the bag.
		 */
		uint8 GetSlot() const noexcept
		{
			return static_cast<uint8>(m_absolute & 0xFF);
		}

		/**
		 * @brief Checks if this slot is an equipment slot.
		 * @return true if slot is in equipment range (slots 0-18 of bag 0).
		 */
		bool IsEquipment() const noexcept;

		/**
		 * @brief Checks if this slot is in an equipped bag (not the main backpack).
		 * @return true if slot is inside an equipped bag container.
		 */
		bool IsBag() const noexcept;

		/**
		 * @brief Checks if this slot is a bag pack slot (where bags are equipped).
		 * @return true if slot can hold a bag container (slots 19-22 of bag 0).
		 */
		bool IsBagPack() const noexcept;

		/**
		 * @brief Checks if this slot is in the main backpack inventory.
		 * @return true if slot is in backpack range (slots 23-38 of bag 0).
		 */
		bool IsInventory() const noexcept;

		/**
		 * @brief Checks if this slot is in the bag bar (where bags are equipped).
		 * @return true if slot is in bag bar range (slots 19-22 of bag 0).
		 */
		bool IsBagBar() const noexcept;

		/**
		 * @brief Checks if this slot is a buyback slot.
		 * @return true if slot is in buyback range (slots 74-85).
		 */
		bool IsBuyBack() const noexcept;

		/**
		 * @brief Equality comparison operator.
		 */
		bool operator==(const InventorySlot& other) const noexcept
		{
			return m_absolute == other.m_absolute;
		}

		/**
		 * @brief Inequality comparison operator.
		 */
		bool operator!=(const InventorySlot& other) const noexcept
		{
			return m_absolute != other.m_absolute;
		}

		/**
		 * @brief Less-than comparison operator for use in ordered containers.
		 */
		bool operator<(const InventorySlot& other) const noexcept
		{
			return m_absolute < other.m_absolute;
		}

	private:
		explicit InventorySlot(uint16 absolute) noexcept
			: m_absolute(absolute)
		{
		}

		uint16 m_absolute;
	};


	/**
	 * @brief Represents a stack of items with count validation.
	 *
	 * This class encapsulates item stack logic, ensuring stack counts are valid
	 * and providing type-safe operations. Follows value object pattern.
	 */
	class ItemStack final
	{
	public:
		/**
		 * @brief Creates an item stack with the specified count.
		 * @param count Number of items in the stack (must be > 0).
		 */
		explicit ItemStack(uint16 count) noexcept
			: m_count(count)
		{
		}

		/**
		 * @brief Gets the number of items in this stack.
		 * @return Item count.
		 */
		uint16 GetCount() const noexcept
		{
			return m_count;
		}

		/**
		 * @brief Checks if this stack can accept additional items.
		 * @param maxStack Maximum allowed stack size.
		 * @return true if stack is not full.
		 */
		bool CanAddStacks(uint16 maxStack) const noexcept
		{
			return m_count < maxStack;
		}

		/**
		 * @brief Calculates how many more items can be added to this stack.
		 * @param maxStack Maximum allowed stack size.
		 * @return Number of available slots (0 if full).
		 */
		uint16 GetAvailableSpace(uint16 maxStack) const noexcept
		{
			return maxStack > m_count ? maxStack - m_count : 0;
		}

		/**
		 * @brief Attempts to add items to this stack.
		 * @param amount Number of items to add.
		 * @param maxStack Maximum allowed stack size.
		 * @return Number of items actually added (may be less than requested).
		 */
		uint16 Add(uint16 amount, uint16 maxStack) noexcept
		{
			const uint16 space = GetAvailableSpace(maxStack);
			const uint16 added = (amount <= space) ? amount : space;
			m_count += added;
			return added;
		}

		/**
		 * @brief Attempts to remove items from this stack.
		 * @param amount Number of items to remove.
		 * @return Number of items actually removed (may be less than requested).
		 */
		uint16 Remove(uint16 amount) noexcept
		{
			const uint16 removed = (amount <= m_count) ? amount : m_count;
			m_count -= removed;
			return removed;
		}

		/**
		 * @brief Checks if this stack is empty.
		 * @return true if count is 0.
		 */
		bool IsEmpty() const noexcept
		{
			return m_count == 0;
		}

		/**
		 * @brief Checks if this stack is full.
		 * @param maxStack Maximum allowed stack size.
		 * @return true if count equals maxStack.
		 */
		bool IsFull(uint16 maxStack) const noexcept
		{
			return m_count >= maxStack;
		}

		/**
		 * @brief Equality comparison operator.
		 */
		bool operator==(const ItemStack& other) const noexcept
		{
			return m_count == other.m_count;
		}

		/**
		 * @brief Inequality comparison operator.
		 */
		bool operator!=(const ItemStack& other) const noexcept
		{
			return m_count != other.m_count;
		}

	private:
		uint16 m_count;
	};


	/**
	 * @brief Represents the count of a specific item type in inventory.
	 *
	 * Provides type safety for item counting operations.
	 */
	class ItemCount final
	{
	public:
		/**
		 * @brief Creates an item count.
		 * @param count The number of items.
		 */
		explicit ItemCount(uint16 count = 0) noexcept
			: m_count(count)
		{
		}

		/**
		 * @brief Gets the item count.
		 * @return Number of items.
		 */
		uint16 Get() const noexcept
		{
			return m_count;
		}

		/**
		 * @brief Adds to the item count.
		 * @param amount Amount to add.
		 */
		void Add(uint16 amount) noexcept
		{
			m_count += amount;
		}

		/**
		 * @brief Subtracts from the item count.
		 * @param amount Amount to subtract.
		 */
		void Subtract(uint16 amount) noexcept
		{
			m_count = (amount <= m_count) ? (m_count - amount) : 0;
		}

		/**
		 * @brief Checks if count is zero.
		 * @return true if no items.
		 */
		bool IsZero() const noexcept
		{
			return m_count == 0;
		}

		/**
		 * @brief Implicit conversion to uint16 for convenience.
		 */
		operator uint16() const noexcept
		{
			return m_count;
		}

	private:
		uint16 m_count;
	};


	/**
	 * @brief Represents available space information for item placement.
	 *
	 * Used during inventory operations to track where items can be placed
	 * and how much space is available.
	 */
	struct SlotAvailability final
	{
		/// Number of completely free slots
		uint16 emptySlots = 0;

		/// Number of partial stacks that can accept more items
		uint16 partialStacks = 0;

		/// Total stack capacity available across all slots
		uint16 availableStackSpace = 0;

		/**
		 * @brief Checks if there's any space available.
		 * @return true if any slot or stack space exists.
		 */
		bool HasSpace() const noexcept
		{
			return emptySlots > 0 || availableStackSpace > 0;
		}

		/**
		 * @brief Checks if enough space exists for the given amount.
		 * @param required Number of items to place.
		 * @return true if sufficient space available.
		 */
		bool CanAccommodate(uint16 required) const noexcept
		{
			return availableStackSpace >= required;
		}
	};


	/**
	 * @brief Result type for inventory operations that may fail.
	 *
	 * Provides a more expressive alternative to returning error codes directly.
	 * Allows chaining operations and better error handling.
	 */
	template<typename T>
	class InventoryResult final
	{
	public:
		/**
		 * @brief Creates a successful result with a value.
		 */
		static InventoryResult Success(T value)
		{
			return InventoryResult(std::move(value), inventory_change_failure::Okay);
		}

		/**
		 * @brief Creates a failed result with an error code.
		 */
		static InventoryResult Failure(InventoryChangeFailure error)
		{
			return InventoryResult(std::nullopt, error);
		}

		/**
		 * @brief Checks if the operation was successful.
		 */
		bool IsSuccess() const noexcept
		{
			return m_error == inventory_change_failure::Okay;
		}

		/**
		 * @brief Checks if the operation failed.
		 */
		bool IsFailure() const noexcept
		{
			return m_error != inventory_change_failure::Okay;
		}

		/**
		 * @brief Gets the error code.
		 */
		InventoryChangeFailure GetError() const noexcept
		{
			return m_error;
		}

		/**
		 * @brief Gets the value if successful.
		 * @return Optional containing the value if operation succeeded.
		 */
		const std::optional<T>& GetValue() const noexcept
		{
			return m_value;
		}

		/**
		 * @brief Executes a function if the result is successful.
		 * @param func Function to execute with the value.
		 * @return Reference to this result for chaining.
		 */
		template<typename Func>
		InventoryResult& OnSuccess(Func&& func)
		{
			if (IsSuccess() && m_value)
			{
				func(*m_value);
			}
			return *this;
		}

		/**
		 * @brief Executes a function if the result is a failure.
		 * @param func Function to execute with the error code.
		 * @return Reference to this result for chaining.
		 */
		template<typename Func>
		InventoryResult& OnFailure(Func&& func)
		{
			if (IsFailure())
			{
				func(m_error);
			}
			return *this;
		}

	private:
		InventoryResult(std::optional<T> value, InventoryChangeFailure error)
			: m_value(std::move(value))
			, m_error(error)
		{
		}

		std::optional<T> m_value;
		InventoryChangeFailure m_error;
	};


	/**
	 * @brief Specialization of InventoryResult for void operations.
	 */
	template<>
	class InventoryResult<void> final
	{
	public:
		/**
		 * @brief Creates a successful result.
		 */
		static InventoryResult Success()
		{
			return InventoryResult(inventory_change_failure::Okay);
		}

		/**
		 * @brief Creates a failed result with an error code.
		 */
		static InventoryResult Failure(InventoryChangeFailure error)
		{
			return InventoryResult(error);
		}

		/**
		 * @brief Checks if the operation was successful.
		 */
		bool IsSuccess() const noexcept
		{
			return m_error == inventory_change_failure::Okay;
		}

		/**
		 * @brief Checks if the operation failed.
		 */
		bool IsFailure() const noexcept
		{
			return m_error != inventory_change_failure::Okay;
		}

		/**
		 * @brief Gets the error code.
		 */
		InventoryChangeFailure GetError() const noexcept
		{
			return m_error;
		}

		/**
		 * @brief Implicit conversion to InventoryChangeFailure for compatibility.
		 */
		operator InventoryChangeFailure() const noexcept
		{
			return m_error;
		}

		/**
		 * @brief Executes a function if the result is successful.
		 * @param func Function to execute.
		 * @return Reference to this result for chaining.
		 */
		template<typename Func>
		InventoryResult& OnSuccess(Func&& func)
		{
			if (IsSuccess())
			{
				func();
			}
			return *this;
		}

		/**
		 * @brief Executes a function if the result is a failure.
		 * @param func Function to execute with the error code.
		 * @return Reference to this result for chaining.
		 */
		template<typename Func>
		InventoryResult& OnFailure(Func&& func)
		{
			if (IsFailure())
			{
				func(m_error);
			}
			return *this;
		}

	private:
		explicit InventoryResult(InventoryChangeFailure error)
			: m_error(error)
		{
		}

		InventoryChangeFailure m_error;
	};
}

// Hash function for InventorySlot to enable use in unordered containers
namespace std
{
	template<>
	struct hash<mmo::InventorySlot>
	{
		size_t operator()(const mmo::InventorySlot& slot) const noexcept
		{
			return std::hash<uint16>{}(slot.GetAbsolute());
		}
	};
}