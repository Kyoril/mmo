#pragma once
#include "base/typedefs.h"

namespace mmo
{
	class SpellEntry
	{
	public:


		[[nodiscard]] uint32 GetId() const { return m_id; }
		[[nodiscard]] const String& GetName() const { return m_name; }
		[[nodiscard]] const String& GetDescription() const { return m_description; }


	private:
		uint32 m_id;
		String m_name;
		String m_description;
	};
}
