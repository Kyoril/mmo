
#include "page.h"

namespace mmo
{
	Page::Page(const PagePosition &position)
		: m_position(position)
	{
	}

	const PagePosition & Page::GetPosition() const
	{
		return m_position;
	}
}
