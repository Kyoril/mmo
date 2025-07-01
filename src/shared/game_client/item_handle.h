#pragma once

#include "base/weak_handle.h"
#include "base/typedefs.h"
#include "client_data/project.h"

namespace mmo
{
	struct ItemInfo;
	class GameItemC;

	class ItemHandle : public WeakHandle<GameItemC>
	{
	public:
		explicit ItemHandle(GameItemC& item, const proto_client::SpellManager& spells);
		explicit ItemHandle();
		virtual ~ItemHandle() override = default;

	public:
		[[nodiscard]] uint32 GetId() const;

		[[nodiscard]] int32 GetStackCount() const;

		[[nodiscard]] bool IsBag() const;

		[[nodiscard]] int32 GetBagSlots() const;

		[[nodiscard]] const char* GetName() const;

		[[nodiscard]] const char* GetDescription() const;

		[[nodiscard]] const char* GetItemClass() const;

		[[nodiscard]] const char* GetItemSubClass() const;

		[[nodiscard]] const char* GetInventoryType() const;

		[[nodiscard]] const char* GetIcon() const;

		[[nodiscard]] int32 GetQuality() const;

		// Weapons
		[[nodiscard]] int32 GetMinDamage() const;

		[[nodiscard]] int32 GetMaxDamage() const;

		[[nodiscard]] float GetAttackSpeed() const;

		[[nodiscard]] float GetDps() const;

		// Armor
		[[nodiscard]] int32 GetArmor() const;

		[[nodiscard]] const ItemInfo* GetEntry() const;

		[[nodiscard]] int32 GetBlock() const;

		[[nodiscard]] int32 GetDurability() const;

		[[nodiscard]] int32 GetMaxDurability() const;

		[[nodiscard]] uint32 GetSellPrice() const;

		[[nodiscard]] const proto_client::SpellEntry* GetSpell(int32 index) const;

		[[nodiscard]] const char* GetSpellTriggerType(int32 index) const;

		[[nodiscard]] const char* GetStatType(int32 index) const;

		[[nodiscard]] int32 GetStatValue(int32 index) const;

	private:
		[[nodiscard]] bool CheckNonNull() const;

		const proto_client::SpellManager* m_spells{ nullptr };
	};
}
