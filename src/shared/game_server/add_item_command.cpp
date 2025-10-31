// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "add_item_command.h"
#include "item_validator.h"
#include "slot_manager.h"
#include "game_server/objects/game_item_s.h"

namespace mmo
{
	AddItemCommand::AddItemCommand(
		IAddItemCommandContext& context,
		std::shared_ptr<GameItemS> item) noexcept
		: m_context(context)
		, m_item(std::move(item))
		, m_targetSlot(std::nullopt)
		, m_resultSlot(std::nullopt)
	{
	}

	AddItemCommand::AddItemCommand(
		IAddItemCommandContext& context,
		std::shared_ptr<GameItemS> item,
		InventorySlot targetSlot) noexcept
		: m_context(context)
		, m_item(std::move(item))
		, m_targetSlot(targetSlot)
		, m_resultSlot(std::nullopt)
	{
	}

	InventoryResult<void> AddItemCommand::Execute()
	{
		if (!m_item)
		{
			return InventoryResult<void>::Failure(inventory_change_failure::InternalBagError);
		}

		if (m_targetSlot.has_value())
		{
			const InventorySlot targetSlot = m_targetSlot.value();
			auto validationResult = ValidateAddition(targetSlot);
			if (validationResult.IsFailure())
			{
				return validationResult;
			}

			m_context.AddItemToSlot(m_item, targetSlot.GetAbsolute());
			m_resultSlot = targetSlot;
			return InventoryResult<void>::Success();
		}
		else
		{
			auto slotResult = FindTargetSlot();
			if (slotResult.IsFailure())
			{
				return InventoryResult<void>::Failure(slotResult.GetError());
			}

			const InventorySlot targetSlot = slotResult.GetValue().value();
			auto validationResult = ValidateAddition(targetSlot);
			if (validationResult.IsFailure())
			{
				return validationResult;
			}

			m_context.AddItemToSlot(m_item, targetSlot.GetAbsolute());
			m_resultSlot = targetSlot;
			return InventoryResult<void>::Success();
		}
	}

	const char* AddItemCommand::GetDescription() const noexcept
	{
		return "Add item to inventory";
	}

	std::optional<InventorySlot> AddItemCommand::GetResultSlot() const noexcept
	{
		return m_resultSlot;
	}

	InventoryResult<InventorySlot> AddItemCommand::FindTargetSlot()
	{
		auto& slotManager = m_context.GetSlotManager();
		const uint16 emptySlot = slotManager.FindFirstEmptySlot();

		if (emptySlot == 0xFFFF)
		{
			return InventoryResult<InventorySlot>::Failure(inventory_change_failure::InventoryFull);
		}

		return InventoryResult<InventorySlot>::Success(InventorySlot::FromAbsolute(emptySlot));
	}

	InventoryResult<void> AddItemCommand::ValidateAddition(InventorySlot slot) const
	{
		auto& validator = m_context.GetValidator();
		return validator.ValidateSlotPlacement(slot, m_item->GetEntry());
	}
}