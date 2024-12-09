#pragma once

#include "base/weak_handle.h"
#include "base/typedefs.h"

namespace mmo
{
	class GameUnitC;

	class UnitHandle : public WeakHandle<GameUnitC>
	{
	public:
		explicit UnitHandle(GameUnitC& unit);
		explicit UnitHandle();
		virtual ~UnitHandle() override = default;

	public:
		[[nodiscard]] int32 GetHealth() const;
		[[nodiscard]] int32 GetMaxHealth() const;
		[[nodiscard]] int32 GetLevel() const;

	private:
		[[nodiscard]] bool CheckNonNull() const;
	};
}