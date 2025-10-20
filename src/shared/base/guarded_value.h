#pragma once

#include "non_copyable.h"

namespace mmo
{
	template <typename RefType, typename AssignedType = RefType>
	struct GuardValue : private NonCopyable
	{
		[[nodiscard]] GuardValue(RefType& referenceValue, const AssignedType& newValue)
			: RefValue(referenceValue), OriginalValue(referenceValue)
		{
			RefValue = newValue;
		}
		~GuardValue()
		{
			RefValue = OriginalValue;
		}

		inline const AssignedType& GetOriginalValue() const
		{
			return OriginalValue;
		}

	private:
		RefType& RefValue;
		AssignedType OriginalValue;
	};

}