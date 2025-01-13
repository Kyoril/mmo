#pragma once

#include "base/weak_handle.h"
#include "base/typedefs.h"

namespace mmo
{
	class GameItemC;

	class ItemHandle : public WeakHandle<GameItemC>
	{
	public:
		explicit ItemHandle(GameItemC& item);
		explicit ItemHandle();
		virtual ~ItemHandle() override = default;

	public:
		[[nodiscard]] int32 GetStackCount() const;

		[[nodiscard]] bool IsBag() const;

		[[nodiscard]] int32 GetBagSlots() const;

		[[nodiscard]] const char* GetItemClass() const;

		[[nodiscard]] const char* GetItemSubClass() const;

		[[nodiscard]] const char* GetInventoryType() const;

	private:
		[[nodiscard]] bool CheckNonNull() const;
	};
}