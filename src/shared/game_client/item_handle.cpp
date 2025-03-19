
#include "item_handle.h"
#include "game_item_c.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ItemHandle::ItemHandle(GameItemC& item, const proto_client::SpellManager& spells)
		: WeakHandle(item, item.removed)
		, m_spells(&spells)
	{
	}

	ItemHandle::ItemHandle() = default;

	int32 ItemHandle::GetStackCount() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetStackCount();
	}

	bool ItemHandle::IsBag() const
	{
		if (!CheckNonNull()) return false;
		return Get()->IsBag();
	}

	int32 ItemHandle::GetBagSlots() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->GetBagSlots();
	}

	const char* ItemHandle::GetName() const
	{
		if (!CheckNonNull()) return nullptr;
		if (!Get()->GetEntry()) return nullptr;

		return Get()->GetEntry()->name.c_str();
	}

	const char* ItemHandle::GetDescription() const
	{
		if (!CheckNonNull()) return nullptr;
		if (!Get()->GetEntry()) return nullptr;

		return Get()->GetEntry()->description.c_str();
	}

	const char* ItemHandle::GetItemClass() const
	{
		if (!CheckNonNull()) return nullptr;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		return info->GetItemClassName();
	}

	const char* ItemHandle::GetItemSubClass() const
	{
		if (!CheckNonNull()) return nullptr;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		return info->GetItemSubClassName();
	}

	const char* ItemHandle::GetInventoryType() const
	{
		if (!CheckNonNull()) return nullptr;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		return info->GetItemInventoryTypeName();
	}

	const char* ItemHandle::GetIcon() const
	{
		static const String s_defaultItemIcon = "Interface\\Icons\\Spells\\S_Attack.htex";

		if (!CheckNonNull()) 
		{
			return nullptr;
		}

		const proto_client::ItemDisplayEntry* displayData = Get()->GetDisplayData();
		if (!displayData || displayData->icon().empty())
		{
			return s_defaultItemIcon.c_str();
		}

		return displayData->icon().c_str();
	}

	int32 ItemHandle::GetQuality() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		return info->quality;
	}

	int32 ItemHandle::GetMinDamage() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;
		if (info->itemClass != item_class::Weapon) return 0;

		return static_cast<int32>(floor(info->damage.min));
	}

	int32 ItemHandle::GetMaxDamage() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;
		if (info->itemClass != item_class::Weapon) return 0;

		return static_cast<int32>(floor(info->damage.max));
	}

	float ItemHandle::GetAttackSpeed() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0.0f;
		if (info->itemClass != item_class::Weapon) return 0.0f;

		return static_cast<float>(info->attackTime) / 1000.0f;
	}

	float ItemHandle::GetDps() const
	{
		const float minDamage = GetMinDamage();
		const float maxDamage = GetMaxDamage();
		const float attackTime = GetAttackSpeed();

		if (attackTime <= 0.0f)
		{
			return 0.0f;
		}

		if (minDamage == maxDamage)
		{
			return minDamage / attackTime;
		}

		return (maxDamage + minDamage) * 0.5f / attackTime;
	}

	int32 ItemHandle::GetArmor() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		return static_cast<int32>(info->armor);
	}

	const ItemInfo* ItemHandle::GetEntry() const
	{
		if (!CheckNonNull()) return nullptr;
		return Get()->GetEntry();
	}

	int32 ItemHandle::GetBlock() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		return static_cast<int32>(info->block);
	}

	int32 ItemHandle::GetDurability() const
	{
		if (!CheckNonNull()) return 0;
		return Get()->Get<uint32>(object_fields::Durability);
	}

	int32 ItemHandle::GetMaxDurability() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		return static_cast<int32>(info->maxdurability);
	}

	uint32 ItemHandle::GetSellPrice() const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		return info->sellPrice;
	}

	const proto_client::SpellEntry* ItemHandle::GetSpell(const int32 index) const
	{
		if (!CheckNonNull()) return nullptr;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		if (!m_spells) return nullptr;
		return m_spells->getById(info->spells[index].spellId);
	}

	const char* ItemHandle::GetSpellTriggerType(int32 index) const
	{
		if (!CheckNonNull()) return nullptr;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		return info->GetSpellTriggerType(index);
	}

	const char* ItemHandle::GetStatType(const int32 index) const
	{
		if (!CheckNonNull()) return nullptr;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return nullptr;

		return info->GetStatType(index);
	}

	int32 ItemHandle::GetStatValue(const int32 index) const
	{
		if (!CheckNonNull()) return 0;

		const ItemInfo* info = Get()->GetEntry();
		if (!info) return 0;

		if (index < 0 || index > std::size(info->stats))
		{
			return 0;
		}

		return static_cast<int32>(info->stats[index].value);
	}

	bool ItemHandle::CheckNonNull() const
	{
		if (Get())
		{
			return true;
		}

		ELOG("Expected non-null item handle!");
		return false;
	}
}
