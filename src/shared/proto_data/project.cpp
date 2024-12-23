#include "project.h"

namespace mmo
{
	namespace proto
	{
		bool SpellHasEffect(const SpellEntry& spell, mmo::SpellEffect type)
		{
			for (const auto& effect : spell.effects())
			{
				if (type == static_cast<mmo::SpellEffect>(effect.type()))
				{
					return true;
				}
			}
			return false;
		}
	}
	
}

