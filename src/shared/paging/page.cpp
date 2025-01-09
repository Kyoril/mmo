
#include "page.h"

namespace mmo
{
	SerializableNavPage::SerializableNavPage(const PagePosition &position)
		: m_position(position)
	{
	}

	const PagePosition & SerializableNavPage::GetPosition() const
	{
		return m_position;
	}
}
